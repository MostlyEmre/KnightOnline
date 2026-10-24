#include "CocoaApp.h"

#include "../../Compat/DInputKeys.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h> // kVK_* virtual key codes
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

using namespace Platform::macOS::Cocoa;

@interface KOGameView : NSView <NSTextInputClient>
@end

namespace
{

struct AppState
{
	NSWindow* window    = nil;
	KOGameView* view    = nil;
	CAMetalLayer* layer = nil;
	id windowDelegate   = nil;
	id appDelegate      = nil;

	int width           = 0; // the game's resolution
	int height          = 0;
	bool fullscreen     = false;
	bool active         = false;

	WindowEvents events;
	TextInputEvents textEvents;
	bool textFocused   = false;
	NSRect textRect    = NSZeroRect; // client coordinates
	bool hasMarkedText = false;

	std::array<uint8_t, 256> keys {};
	NSCursor* cursor       = nil;
	NSCursor* blank        = nil;
	CGFloat wheelRemainder = 0.0;
};

AppState& App()
{
	static AppState state;
	return state;
}

// Mac virtual key code (kVK_*) -> DirectInput scan code (DIK_*); 0 = unmapped.
std::array<uint8_t, 128> BuildKeyMap()
{
	std::array<uint8_t, 128> map {};
	auto set = [&map](int vk, int dik)
	{ map[static_cast<size_t>(vk)] = static_cast<uint8_t>(dik); };

	set(kVK_ANSI_A, DIK_A);
	set(kVK_ANSI_B, DIK_B);
	set(kVK_ANSI_C, DIK_C);
	set(kVK_ANSI_D, DIK_D);
	set(kVK_ANSI_E, DIK_E);
	set(kVK_ANSI_F, DIK_F);
	set(kVK_ANSI_G, DIK_G);
	set(kVK_ANSI_H, DIK_H);
	set(kVK_ANSI_I, DIK_I);
	set(kVK_ANSI_J, DIK_J);
	set(kVK_ANSI_K, DIK_K);
	set(kVK_ANSI_L, DIK_L);
	set(kVK_ANSI_M, DIK_M);
	set(kVK_ANSI_N, DIK_N);
	set(kVK_ANSI_O, DIK_O);
	set(kVK_ANSI_P, DIK_P);
	set(kVK_ANSI_Q, DIK_Q);
	set(kVK_ANSI_R, DIK_R);
	set(kVK_ANSI_S, DIK_S);
	set(kVK_ANSI_T, DIK_T);
	set(kVK_ANSI_U, DIK_U);
	set(kVK_ANSI_V, DIK_V);
	set(kVK_ANSI_W, DIK_W);
	set(kVK_ANSI_X, DIK_X);
	set(kVK_ANSI_Y, DIK_Y);
	set(kVK_ANSI_Z, DIK_Z);
	set(kVK_ANSI_0, DIK_0);
	set(kVK_ANSI_1, DIK_1);
	set(kVK_ANSI_2, DIK_2);
	set(kVK_ANSI_3, DIK_3);
	set(kVK_ANSI_4, DIK_4);
	set(kVK_ANSI_5, DIK_5);
	set(kVK_ANSI_6, DIK_6);
	set(kVK_ANSI_7, DIK_7);
	set(kVK_ANSI_8, DIK_8);
	set(kVK_ANSI_9, DIK_9);
	set(kVK_ANSI_Minus, DIK_MINUS);
	set(kVK_ANSI_Equal, DIK_EQUALS);
	set(kVK_ANSI_LeftBracket, DIK_LBRACKET);
	set(kVK_ANSI_RightBracket, DIK_RBRACKET);
	set(kVK_ANSI_Semicolon, DIK_SEMICOLON);
	set(kVK_ANSI_Quote, DIK_APOSTROPHE);
	set(kVK_ANSI_Grave, DIK_GRAVE);
	set(kVK_ANSI_Backslash, DIK_BACKSLASH);
	set(kVK_ANSI_Comma, DIK_COMMA);
	set(kVK_ANSI_Period, DIK_PERIOD);
	set(kVK_ANSI_Slash, DIK_SLASH);
	set(kVK_ISO_Section, DIK_GRAVE);
	set(kVK_Return, DIK_RETURN);
	set(kVK_Tab, DIK_TAB);
	set(kVK_Space, DIK_SPACE);
	set(kVK_Delete, DIK_BACK);
	set(kVK_ForwardDelete, DIK_DELETE);
	set(kVK_Help, DIK_INSERT);
	set(kVK_Escape, DIK_ESCAPE);
	set(kVK_Home, DIK_HOME);
	set(kVK_End, DIK_END);
	set(kVK_PageUp, DIK_PRIOR);
	set(kVK_PageDown, DIK_NEXT);
	set(kVK_LeftArrow, DIK_LEFT);
	set(kVK_RightArrow, DIK_RIGHT);
	set(kVK_UpArrow, DIK_UP);
	set(kVK_DownArrow, DIK_DOWN);
	set(kVK_Shift, DIK_LSHIFT);
	set(kVK_RightShift, DIK_RSHIFT);
	set(kVK_Control, DIK_LCONTROL);
	set(kVK_RightControl, DIK_RCONTROL);
	set(kVK_Option, DIK_LMENU);
	set(kVK_RightOption, DIK_RMENU);
	set(kVK_Command, DIK_LWIN);
	set(kVK_RightCommand, DIK_RWIN);
	set(kVK_F1, DIK_F1);
	set(kVK_F2, DIK_F2);
	set(kVK_F3, DIK_F3);
	set(kVK_F4, DIK_F4);
	set(kVK_F5, DIK_F5);
	set(kVK_F6, DIK_F6);
	set(kVK_F7, DIK_F7);
	set(kVK_F8, DIK_F8);
	set(kVK_F9, DIK_F9);
	set(kVK_F10, DIK_F10);
	set(kVK_F11, DIK_F11);
	set(kVK_F12, DIK_F12);
	set(kVK_ANSI_Keypad0, DIK_NUMPAD0);
	set(kVK_ANSI_Keypad1, DIK_NUMPAD1);
	set(kVK_ANSI_Keypad2, DIK_NUMPAD2);
	set(kVK_ANSI_Keypad3, DIK_NUMPAD3);
	set(kVK_ANSI_Keypad4, DIK_NUMPAD4);
	set(kVK_ANSI_Keypad5, DIK_NUMPAD5);
	set(kVK_ANSI_Keypad6, DIK_NUMPAD6);
	set(kVK_ANSI_Keypad7, DIK_NUMPAD7);
	set(kVK_ANSI_Keypad8, DIK_NUMPAD8);
	set(kVK_ANSI_Keypad9, DIK_NUMPAD9);
	set(kVK_ANSI_KeypadDecimal, DIK_DECIMAL);
	set(kVK_ANSI_KeypadMultiply, DIK_MULTIPLY);
	set(kVK_ANSI_KeypadPlus, DIK_ADD);
	set(kVK_ANSI_KeypadMinus, DIK_SUBTRACT);
	set(kVK_ANSI_KeypadDivide, DIK_DIVIDE);
	set(kVK_ANSI_KeypadEnter, DIK_NUMPADENTER);
	set(kVK_ANSI_KeypadClear, DIK_NUMLOCK);
	return map;
}

uint8_t ScanCode(unsigned short keyCode)
{
	static const std::array<uint8_t, 128> s_map = BuildKeyMap();
	return keyCode < s_map.size() ? s_map[keyCode] : 0;
}

// Device-dependent modifier bits (IOKit's NX_DEVICE*KEYMASK), which tell left from right.
constexpr NSUInteger DeviceLeftControl  = 0x00000001;
constexpr NSUInteger DeviceLeftShift    = 0x00000002;
constexpr NSUInteger DeviceRightShift   = 0x00000004;
constexpr NSUInteger DeviceLeftCommand  = 0x00000008;
constexpr NSUInteger DeviceRightCommand = 0x00000010;
constexpr NSUInteger DeviceLeftOption   = 0x00000020;
constexpr NSUInteger DeviceRightOption  = 0x00000040;
constexpr NSUInteger DeviceRightControl = 0x00002000;

void TrackKeys(NSEvent* event)
{
	AppState& app = App();
	if (event.window != app.window)
		return;

	switch (event.type)
	{
		case NSEventTypeKeyDown:
		case NSEventTypeKeyUp:
			if (uint8_t dik = ScanCode(event.keyCode); dik != 0)
				app.keys[dik] = event.type == NSEventTypeKeyDown ? 0x80 : 0;
			break;

		case NSEventTypeFlagsChanged:
		{
			NSUInteger flags       = event.modifierFlags;
			app.keys[DIK_LSHIFT]   = (flags & DeviceLeftShift) != 0 ? 0x80 : 0;
			app.keys[DIK_RSHIFT]   = (flags & DeviceRightShift) != 0 ? 0x80 : 0;
			app.keys[DIK_LCONTROL] = (flags & DeviceLeftControl) != 0 ? 0x80 : 0;
			app.keys[DIK_RCONTROL] = (flags & DeviceRightControl) != 0 ? 0x80 : 0;
			app.keys[DIK_LMENU]    = (flags & DeviceLeftOption) != 0 ? 0x80 : 0;
			app.keys[DIK_RMENU]    = (flags & DeviceRightOption) != 0 ? 0x80 : 0;
			app.keys[DIK_LWIN]     = (flags & DeviceLeftCommand) != 0 ? 0x80 : 0;
			app.keys[DIK_RWIN]     = (flags & DeviceRightCommand) != 0 ? 0x80 : 0;
			break;
		}

		default:
			break;
	}
}

CGFloat MainDisplayHeight()
{
	NSArray<NSScreen*>* screens = NSScreen.screens;
	return screens.count > 0 ? screens[0].frame.size.height : 0.0;
}

// Where the game's image sits on screen (Cocoa global coordinates: points, origin bottom left).
NSRect ImageScreenRect()
{
	AppState& app = App();
	if (app.window == nil || app.view == nil)
		return NSZeroRect;

	NSRect viewRect = [app.window convertRectToScreen:[app.view convertRect:app.view.bounds
																	 toView:nil]];
	if (!app.fullscreen || app.width <= 0 || app.height <= 0)
		return viewRect;

	// Aspect fit, as the layer's contentsGravity does.
	CGFloat scale = MIN(viewRect.size.width / app.width, viewRect.size.height / app.height);
	CGFloat w = app.width * scale, h = app.height * scale;
	return NSMakeRect(viewRect.origin.x + (viewRect.size.width - w) / 2,
		viewRect.origin.y + (viewRect.size.height - h) / 2, w, h);
}

NSString* ToNSString(const std::string& text)
{
	NSString* s = [[NSString alloc] initWithBytes:text.data()
										   length:text.size()
										 encoding:NSUTF8StringEncoding];
	return s != nil ? s : @"";
}

std::string ToUtf8(id text)
{
	NSString* s      = [text isKindOfClass:[NSAttributedString class]]
						   ? [(NSAttributedString*) text string]
						   : (NSString*) text;
	const char* utf8 = s.UTF8String;
	return utf8 != nullptr ? utf8 : "";
}

uint16_t ReadU16(const std::vector<uint8_t>& data, size_t at)
{
	return static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
}

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t at)
{
	return static_cast<uint32_t>(data[at] | (data[at + 1] << 8) | (data[at + 2] << 16)
								 | (static_cast<uint32_t>(data[at + 3]) << 24));
}

// A Windows .cur file's first image as RGBA, plus its hotspot.
NSCursor* DecodeCursor(const std::vector<uint8_t>& data)
{
	if (data.size() < 6 + 16 || ReadU16(data, 0) != 0 || ReadU16(data, 2) != 2
		|| ReadU16(data, 4) == 0)
		return nil;

	int hotX = ReadU16(data, 6 + 4), hotY = ReadU16(data, 6 + 6);
	uint32_t size = ReadU32(data, 6 + 8), offset = ReadU32(data, 6 + 12);
	if (offset + size > data.size() || size < 40)
		return nil;

	NSImage* image              = nil;
	static const uint8_t png[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	if (std::memcmp(&data[offset], png, sizeof(png)) == 0)
	{
		image = [[NSImage alloc] initWithData:[NSData dataWithBytes:&data[offset] length:size]];
	}
	else
	{
		// BITMAPINFOHEADER, colour table, XOR bitmap, AND mask; rows bottom-up; height counts both.
		uint32_t headerSize = ReadU32(data, offset);
		int width           = static_cast<int>(ReadU32(data, offset + 4));
		int height          = static_cast<int>(ReadU32(data, offset + 8)) / 2;
		int bits            = ReadU16(data, offset + 14);
		uint32_t colorsUsed = ReadU32(data, offset + 32);
		if (width <= 0 || width > 256 || height <= 0 || height > 256
			|| (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32))
			return nil;

		size_t paletteCount = bits <= 8 ? (colorsUsed != 0 ? colorsUsed : (1u << bits)) : 0;
		size_t paletteAt    = offset + headerSize;
		size_t xorStride    = ((static_cast<size_t>(width) * bits + 31) / 32) * 4;
		size_t andStride    = ((static_cast<size_t>(width) + 31) / 32) * 4;
		size_t xorAt        = paletteAt + paletteCount * 4;
		size_t andAt        = xorAt + xorStride * height;
		if (andAt + andStride * height > offset + size)
			return nil;

		NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
			initWithBitmapDataPlanes:nullptr
						  pixelsWide:width
						  pixelsHigh:height
					   bitsPerSample:8
					 samplesPerPixel:4
							hasAlpha:YES
							isPlanar:NO
					  colorSpaceName:NSDeviceRGBColorSpace
						 bytesPerRow:width * 4
						bitsPerPixel:32];
		uint8_t* out          = rep.bitmapData;

		bool anyAlpha         = false;
		if (bits == 32)
			for (int y = 0; y < height && !anyAlpha; y++)
				for (int x = 0; x < width && !anyAlpha; x++)
					anyAlpha = data[xorAt + y * xorStride + x * 4 + 3] != 0;

		for (int y = 0; y < height; y++)
		{
			size_t row = static_cast<size_t>(height - 1 - y); // bottom-up
			for (int x = 0; x < width; x++)
			{
				uint8_t r = 0, g = 0, b = 0, a = 255;
				const uint8_t* px = &data[xorAt + row * xorStride];
				if (bits == 32 || bits == 24)
				{
					const uint8_t* p = px + x * (bits / 8);
					b                = p[0];
					g                = p[1];
					r                = p[2];
					if (bits == 32 && anyAlpha)
						a = p[3];
				}
				else
				{
					int index = 0;
					if (bits == 8)
						index = px[x];
					else if (bits == 4)
						index = (px[x / 2] >> ((x % 2) == 0 ? 4 : 0)) & 0x0F;
					else
						index = (px[x / 8] >> (7 - (x % 8))) & 0x01;
					if (static_cast<size_t>(index) < paletteCount)
					{
						const uint8_t* q = &data[paletteAt + index * 4];
						b                = q[0];
						g                = q[1];
						r                = q[2];
					}
				}

				if (!(bits == 32 && anyAlpha))
				{
					bool transparent = ((data[andAt + row * andStride + x / 8] >> (7 - (x % 8)))
										   & 1)
									   != 0;
					if (transparent)
						r = g = b = a = 0; // (an inverting pixel can't be expressed; leave it out)
				}

				uint8_t* o = out + (static_cast<size_t>(y) * width + x) * 4;
				o[0]       = r;
				o[1]       = g;
				o[2]       = b;
				o[3]       = a;
			}
		}

		image = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
		[image addRepresentation:rep];
	}

	if (image == nil)
		return nil;

	return [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(hotX, hotY)];
}

NSCursor* BlankCursor()
{
	AppState& app = App();
	if (app.blank == nil)
	{
		NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(1, 1)];
		[image lockFocus];
		[[NSColor clearColor] set];
		NSRectFill(NSMakeRect(0, 0, 1, 1));
		[image unlockFocus];
		app.blank = [[NSCursor alloc] initWithImage:image hotSpot:NSZeroPoint];
	}

	return app.blank;
}

} // namespace

@implementation KOGameView

- (BOOL)wantsUpdateLayer
{
	return YES;
}

- (CALayer*)makeBackingLayer
{
	return [CAMetalLayer layer];
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
	return YES;
}

- (void)resetCursorRects
{
	NSCursor* cursor = App().cursor != nil ? App().cursor : [NSCursor arrowCursor];
	[self addCursorRect:self.bounds cursor:cursor];
}

- (void)keyDown:(NSEvent*)event
{
	// Key state is tracked as events are pumped; text goes to the input field when it has focus.
	if (App().textFocused)
		[self interpretKeyEvents:@[ event ]];
}

- (void)keyUp:(NSEvent*)event
{
}

- (void)flagsChanged:(NSEvent*)event
{
}

- (void)scrollWheel:(NSEvent*)event
{
	AppState& app = App();
	if (!app.events.OnMouseWheel)
		return;

	// Notches for wheels; for precise (trackpad) scrolling, one notch per 10 points.
	CGFloat notches     = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 10.0
														  : event.scrollingDeltaY;
	notches            += app.wheelRemainder;
	int delta           = static_cast<int>(notches * 120.0);
	app.wheelRemainder  = notches - delta / 120.0;
	if (delta != 0)
		app.events.OnMouseWheel(delta);
}

// NSTextInputClient

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
	AppState& app     = App();
	app.hasMarkedText = false;
	if (app.textFocused && app.textEvents.OnInsertText)
		app.textEvents.OnInsertText(ToUtf8(string));
}

- (void)doCommandBySelector:(SEL)selector
{
	AppState& app = App();
	if (app.textFocused && app.textEvents.OnCommand)
		app.textEvents.OnCommand(NSStringFromSelector(selector).UTF8String);
}

- (void)setMarkedText:(id)string
		selectedRange:(NSRange)selectedRange
	 replacementRange:(NSRange)replacementRange
{
	AppState& app      = App();
	std::string marked = ToUtf8(string);
	app.hasMarkedText  = !marked.empty();
	if (app.textFocused && app.textEvents.OnSetMarkedText)
		app.textEvents.OnSetMarkedText(marked);
}

- (void)unmarkText
{
	AppState& app     = App();
	app.hasMarkedText = false;
	if (app.textEvents.OnUnmarkText)
		app.textEvents.OnUnmarkText();
}

- (NSRange)selectedRange
{
	return NSMakeRange(0, 0);
}

- (NSRange)markedRange
{
	return App().hasMarkedText ? NSMakeRange(0, 1) : NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText
{
	return App().hasMarkedText;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
											   actualRange:(NSRangePointer)actualRange
{
	return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
	return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
	// Below the edit box, where the IME's candidate window can go.
	AppState& app = App();
	NSRect image  = ImageScreenRect();
	if (app.width <= 0 || app.height <= 0)
		return image;

	CGFloat sx = image.size.width / app.width, sy = image.size.height / app.height;
	NSRect r = app.textRect;
	return NSMakeRect(image.origin.x + r.origin.x * sx,
		image.origin.y + image.size.height - (r.origin.y + r.size.height) * sy, r.size.width * sx,
		r.size.height * sy);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
	return NSNotFound;
}

@end

@interface KOWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation KOWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender
{
	// The game decides; when it quits, the event loop ends.
	if (App().events.OnCloseRequested)
		App().events.OnCloseRequested();
	return NO;
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
	AppState& app = App();
	app.active    = true;
	if (app.events.OnActivate)
		app.events.OnActivate(true);
}

- (void)windowDidResignKey:(NSNotification*)notification
{
	// Keys released while away never send key-ups.
	AppState& app = App();
	app.active    = false;
	app.keys.fill(0);
	if (app.events.OnActivate)
		app.events.OnActivate(false);
}

@end

@interface KOAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation KOAppDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
	// Cmd-Q: ask the game like closing the window does.
	if (App().events.OnCloseRequested)
		App().events.OnCloseRequested();
	return NSTerminateCancel;
}

@end

namespace Platform::macOS::Cocoa
{

bool CreateGameWindow(const std::string& szTitle, int iWidth, int iHeight, bool bFullscreen,
	bool bTakeFocus, const WindowEvents& events)
{
	AppState& app = App();
	if (app.window != nil || iWidth <= 0 || iHeight <= 0)
		return false;

	@autoreleasepool
	{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		app.appDelegate     = [[KOAppDelegate alloc] init];
		NSApp.delegate      = app.appDelegate;

		NSString* title     = ToNSString(szTitle);
		NSMenu* menuBar     = [[NSMenu alloc] init];
		NSMenuItem* appItem = [[NSMenuItem alloc] init];
		[menuBar addItem:appItem];
		NSMenu* appMenu = [[NSMenu alloc] init];
		[appMenu addItemWithTitle:[@"Quit " stringByAppendingString:title]
						   action:@selector(terminate:)
					keyEquivalent:@"q"];
		appItem.submenu = appMenu;
		NSApp.mainMenu  = menuBar;

		[NSApp finishLaunching];

		app.width               = iWidth;
		app.height              = iHeight;
		app.fullscreen          = bFullscreen;
		app.events              = events;

		NSRect content          = NSMakeRect(0, 0, iWidth, iHeight);
		NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
								  | NSWindowStyleMaskMiniaturizable;
		if (bFullscreen)
		{
			content = NSScreen.mainScreen.frame;
			style   = NSWindowStyleMaskBorderless;
		}

		app.window = [[NSWindow alloc] initWithContentRect:content
												 styleMask:style
												   backing:NSBackingStoreBuffered
													 defer:NO];
		if (app.window == nil)
			return false;

		app.window.title                   = title;
		app.window.releasedWhenClosed      = NO;
		app.window.acceptsMouseMovedEvents = YES;
		app.windowDelegate                 = [[KOWindowDelegate alloc] init];
		app.window.delegate                = app.windowDelegate;

		app.view                           = [[KOGameView alloc]
            initWithFrame:NSMakeRect(0, 0, content.size.width, content.size.height)];
		app.view.wantsLayer                = YES;
		app.window.contentView             = app.view;
		[app.window makeFirstResponder:app.view];

		app.layer                 = (CAMetalLayer*) app.view.layer;
		app.layer.device          = MTLCreateSystemDefaultDevice();
		app.layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
		app.layer.drawableSize    = CGSizeMake(iWidth, iHeight);
		app.layer.framebufferOnly = NO;
		app.layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
		if (bFullscreen)
		{
			app.layer.contentsGravity     = kCAGravityResizeAspect;
			app.layer.magnificationFilter = kCAFilterLinear;
			app.window.level              = NSMainMenuWindowLevel + 1;
			NSApp.presentationOptions     = NSApplicationPresentationHideDock
										| NSApplicationPresentationHideMenuBar;
		}
		else
		{
			// Points map 1:1 to game pixels, so on Retina each pixel is shown as an exact 2x2 block.
			app.layer.contentsGravity     = kCAGravityResize;
			app.layer.magnificationFilter = kCAFilterNearest;

			// Reopen where the player last left the window, like other Mac apps.
			if (![app.window setFrameUsingName:@"KnightOnline"])
				[app.window center];
			app.window.frameAutosaveName = @"KnightOnline";
		}

		// Warping the cursor (SetCursorPos) shouldn't swallow the mouse movement that follows.
		CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
		if (source != nullptr)
		{
			CGEventSourceSetLocalEventsSuppressionInterval(source, 0.0);
			CFRelease(source);
		}

		if (bTakeFocus)
		{
			[app.window makeKeyAndOrderFront:nil];
			[NSApp activateIgnoringOtherApps:YES];
		}
		else
		{
			[app.window orderFrontRegardless];
		}
	}

	return true;
}

void PumpEvents()
{
	@autoreleasepool
	{
		while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
												   untilDate:nil
													  inMode:NSDefaultRunLoopMode
													 dequeue:YES])
		{
			TrackKeys(event);
			[NSApp sendEvent:event];
		}
	}
}

void* GameLayer()
{
	return (__bridge void*) App().layer;
}

int ClientWidth()
{
	return App().width;
}

int ClientHeight()
{
	return App().height;
}

void CursorScreenPosition(int& x, int& y)
{
	NSPoint p = NSEvent.mouseLocation;
	x         = static_cast<int>(floor(p.x));
	y         = static_cast<int>(floor(MainDisplayHeight() - p.y));
}

void ScreenToClient(int& x, int& y)
{
	AppState& app = App();
	NSRect image  = ImageScreenRect();
	if (image.size.width <= 0 || image.size.height <= 0)
		return;

	CGFloat cocoaY = MainDisplayHeight() - y;
	CGFloat cx     = (x - image.origin.x) * app.width / image.size.width;
	CGFloat cy     = (image.origin.y + image.size.height - cocoaY) * app.height / image.size.height;
	x              = static_cast<int>(floor(cx));
	y              = static_cast<int>(floor(cy));
}

void ClientToScreen(int& x, int& y)
{
	AppState& app = App();
	NSRect image  = ImageScreenRect();
	if (app.width <= 0 || app.height <= 0)
		return;

	CGFloat sx     = image.origin.x + x * image.size.width / app.width;
	CGFloat cocoaY = image.origin.y + image.size.height - y * image.size.height / app.height;
	x              = static_cast<int>(floor(sx));
	y              = static_cast<int>(floor(MainDisplayHeight() - cocoaY));
}

void WarpCursor(int iScreenX, int iScreenY)
{
	// Quartz display coordinates: points, origin at the top left of the main display (as ours).
	CGWarpMouseCursorPosition(CGPointMake(iScreenX, iScreenY));
	CGAssociateMouseAndMouseCursorPosition(true);
}

void WindowScreenRect(int& left, int& top, int& right, int& bottom)
{
	AppState& app = App();
	NSRect frame  = app.window != nil ? app.window.frame : NSZeroRect;
	CGFloat h     = MainDisplayHeight();
	left          = static_cast<int>(frame.origin.x);
	top           = static_cast<int>(h - (frame.origin.y + frame.size.height));
	right         = static_cast<int>(frame.origin.x + frame.size.width);
	bottom        = static_cast<int>(h - frame.origin.y);
}

bool IsActive()
{
	return App().active;
}

void ActivateGameWindow()
{
	@autoreleasepool
	{
		if (App().window == nil)
			return;

		[App().window makeKeyAndOrderFront:nil];
		[NSApp activateIgnoringOtherApps:YES];
	}
}

uint32_t PressedMouseButtons()
{
	return static_cast<uint32_t>(NSEvent.pressedMouseButtons);
}

bool IsOptionDown()
{
	return (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
}

bool IsControlDown()
{
	return (NSEvent.modifierFlags & NSEventModifierFlagControl) != 0;
}

bool IsKeyDown(int iScanCode)
{
	AppState& app = App();
	return iScanCode >= 0 && iScanCode < 256 && app.active
		   && app.keys[static_cast<size_t>(iScanCode)] != 0;
}

void KeyboardState(uint8_t (&keys)[256])
{
	std::memcpy(keys, App().keys.data(), sizeof(keys));
}

uint32_t DoubleClickIntervalMs()
{
	return static_cast<uint32_t>(NSEvent.doubleClickInterval * 1000.0);
}

void* LoadCursorFile(const std::string& szPath)
{
	@autoreleasepool
	{
		NSData* data = [NSData dataWithContentsOfFile:ToNSString(szPath)];
		if (data == nil)
			return nullptr;

		std::vector<uint8_t> bytes(static_cast<const uint8_t*>(data.bytes),
			static_cast<const uint8_t*>(data.bytes) + data.length);
		NSCursor* cursor = DecodeCursor(bytes);
		return cursor != nil ? (__bridge_retained void*) cursor : nullptr;
	}
}

void SetCursor(void* pCursor)
{
	AppState& app    = App();
	NSCursor* cursor = pCursor != nullptr ? (__bridge NSCursor*) pCursor : BlankCursor();
	if (app.cursor == cursor)
		return;

	app.cursor = cursor;
	if (app.window != nil)
		[app.window invalidateCursorRectsForView:app.view];

	NSPoint p = [app.window mouseLocationOutsideOfEventStream];
	if (app.view != nil && NSPointInRect([app.view convertPoint:p fromView:nil], app.view.bounds))
		[cursor set];
}

void SetCursorVisible(bool bVisible)
{
	if (bVisible)
		[NSCursor unhide];
	else
		[NSCursor hide];
}

void SetTextInputFocus(bool bFocused, int iLeft, int iTop, int iRight, int iBottom)
{
	AppState& app = App();
	if (!bFocused && app.textFocused && app.view != nil)
		[app.view.inputContext discardMarkedText];

	app.textFocused   = bFocused;
	app.hasMarkedText = false;
	app.textRect      = NSMakeRect(iLeft, iTop, iRight - iLeft, iBottom - iTop);
}

void SetTextInputEvents(const TextInputEvents& events)
{
	App().textEvents = events;
}

void ShowAlert(const std::string& szCaption, const std::string& szText)
{
	@autoreleasepool
	{
		NSAlert* alert        = [[NSAlert alloc] init];
		alert.messageText     = ToNSString(szCaption);
		alert.informativeText = ToNSString(szText);
		[alert addButtonWithTitle:@"OK"];
		[alert runModal];
	}
}

void OpenURL(const std::string& szURL)
{
	@autoreleasepool
	{
		NSString* target = ToNSString(szURL);
		NSURL* url       = [target containsString:@"://"] ? [NSURL URLWithString:target]
														  : [NSURL fileURLWithPath:target];
		if (url != nil)
			[[NSWorkspace sharedWorkspace] openURL:url];
	}
}

std::string PreferredLanguage()
{
	@autoreleasepool
	{
		NSString* language = NSLocale.preferredLanguages.firstObject;
		return language != nil ? language.UTF8String : "en-US";
	}
}

std::string ResourceDirectory()
{
	@autoreleasepool
	{
		NSString* path = NSBundle.mainBundle.resourcePath;
		return path != nil ? path.UTF8String : "";
	}
}

} // namespace Platform::macOS::Cocoa

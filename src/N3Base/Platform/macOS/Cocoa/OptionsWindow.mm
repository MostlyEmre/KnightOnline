#include "OptionsWindow.h"

#import <AppKit/AppKit.h>

#include <array>
#include <utility>

using Platform::macOS::GameOptions;

namespace
{

// The game's own resolutions (WarFareMain snaps the height for these widths), after "fit".
constexpr std::array<std::pair<int, int>, 5> Resolutions = { {
	{ 1024, 768 },
	{ 1280, 1024 },
	{ 1366, 768 },
	{ 1600, 1200 },
	{ 1920, 1080 },
} };

NSTextField* Label(NSString* text)
{
	NSTextField* label = [NSTextField labelWithString:text];
	label.alignment    = NSTextAlignmentRight;
	return label;
}

NSTextField* Note(NSString* text)
{
	NSTextField* note = [NSTextField wrappingLabelWithString:text];
	note.font         = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
	note.textColor    = NSColor.secondaryLabelColor;
	[note.widthAnchor constraintLessThanOrEqualToConstant:330].active = YES;
	return note;
}

} // namespace

@interface KOOptionsController : NSObject <NSWindowDelegate>
@property(nonatomic) GameOptions options;
@property(nonatomic) Platform::macOS::Cocoa::OptionsWindowHooks hooks;
@property(nonatomic) int fitWidth;
@property(nonatomic) int fitHeight;
@property(nonatomic) BOOL afterQuit;
@property(nonatomic) BOOL startGame;
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSTabViewController* tabs;
// Display
@property(nonatomic, strong) NSPopUpButton* mode;
@property(nonatomic, strong) NSPopUpButton* resolution;
@property(nonatomic, strong) NSPopUpButton* windowScale;
@property(nonatomic, strong) NSPopUpButton* scaling;
// Graphics
@property(nonatomic, strong) NSSlider* viewDistance;
@property(nonatomic, strong) NSTextField* viewDistanceValue;
@property(nonatomic, strong) NSButton* shadows;
@property(nonatomic, strong) NSPopUpButton* textureChr;
@property(nonatomic, strong) NSPopUpButton* textureShape;
@property(nonatomic, strong) NSPopUpButton* textureTerrain;
@property(nonatomic, strong) NSButton* systemCursor;
// Sound
@property(nonatomic, strong) NSButton* music;
@property(nonatomic, strong) NSButton* soundEffects;
@property(nonatomic, strong) NSSlider* soundDistance;
@property(nonatomic, strong) NSTextField* soundDistanceValue;
// Controls
@property(nonatomic, strong) NSButton* swipeTurns;
@property(nonatomic, strong) NSSlider* swipeSpeed;
@property(nonatomic, strong) NSTextField* swipeSpeedValue;
@property(nonatomic, strong) NSButton* swipeReversed;
@property(nonatomic, strong) NSButton* controlClick;
// Performance
@property(nonatomic, strong) NSButton* vsync;
@property(nonatomic, strong) NSPopUpButton* maxFps;
@property(nonatomic, strong) NSPopUpButton* backgroundFps;
@property(nonatomic, strong) NSButton* keepAwake;
@end

@implementation KOOptionsController

- (NSPopUpButton*)popup:(NSArray<NSString*>*)titles tags:(NSArray<NSNumber*>*)tags
{
	NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
	for (NSUInteger i = 0; i < titles.count; i++)
	{
		[popup addItemWithTitle:titles[i]];
		popup.lastItem.tag = tags[i].integerValue;
	}
	popup.target = self;
	popup.action = @selector(changed:);
	return popup;
}

- (NSButton*)checkbox:(NSString*)title
{
	return [NSButton checkboxWithTitle:title target:self action:@selector(changed:)];
}

- (NSSlider*)slider:(double)minimum max:(double)maximum ticks:(NSInteger)ticks
{
	NSSlider* slider                                          = [NSSlider sliderWithValue:minimum
                                        minValue:minimum
                                        maxValue:maximum
                                          target:self
                                          action:@selector(changed:)];
	slider.numberOfTickMarks                                  = ticks;
	slider.allowsTickMarkValuesOnly                           = YES;
	[slider.widthAnchor constraintEqualToConstant:200].active = YES;
	return slider;
}

// One tab: a grid of label / control rows, then notes underneath.
- (NSTabViewItem*)tab:(NSString*)title
			   symbol:(NSString*)symbol
				 rows:(NSArray<NSArray<NSView*>*>*)rows
{
	NSGridView* grid                               = [NSGridView gridViewWithViews:rows];
	grid.rowSpacing                                = 10;
	grid.columnSpacing                             = 10;
	[grid columnAtIndex:0].xPlacement              = NSGridCellPlacementTrailing;
	grid.translatesAutoresizingMaskIntoConstraints = NO;
	for (NSInteger i = 0; i < grid.numberOfRows; i++)
		[grid rowAtIndex:i].yPlacement = NSGridCellPlacementCenter;

	NSViewController* page = [[NSViewController alloc] init];
	page.view              = [[NSView alloc] initWithFrame:NSZeroRect];
	[page.view addSubview:grid];
	// Every tab the same width (wide enough for all five toolbar items), rows packed at the top.
	[NSLayoutConstraint activateConstraints:@[
		[page.view.widthAnchor constraintEqualToConstant:620],
		[grid.topAnchor constraintEqualToAnchor:page.view.topAnchor constant:20],
		[grid.bottomAnchor constraintLessThanOrEqualToAnchor:page.view.bottomAnchor constant:-20],
		[grid.leadingAnchor constraintEqualToAnchor:page.view.leadingAnchor constant:30],
		[grid.trailingAnchor constraintLessThanOrEqualToAnchor:page.view.trailingAnchor
													  constant:-30],
	]];
	[grid columnAtIndex:0].width = 170;
	page.title                   = title;

	NSTabViewItem* item          = [NSTabViewItem tabViewItemWithViewController:page];
	item.label                   = title;
	item.image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:title];
	return item;
}

- (void)build
{
	NSView* empty = NSGridCell.emptyContentView; // a placeholder that can fill any number of cells

	// Display
	self.mode     = [self popup:@[ @"Fullscreen", @"Window" ] tags:@[ @0, @1 ]];
	NSMutableArray<NSString*>* resTitles = [NSMutableArray
		arrayWithObject:[NSString stringWithFormat:@"Fit the screen (%d × %d)", self.fitWidth,
							self.fitHeight]];
	NSMutableArray<NSNumber*>* resTags   = [NSMutableArray arrayWithObject:@(-1)];
	for (size_t i = 0; i < Resolutions.size(); i++)
	{
		[resTitles addObject:[NSString stringWithFormat:@"%d × %d", Resolutions[i].first,
								 Resolutions[i].second]];
		[resTags addObject:@(static_cast<int>(i))];
	}
	self.resolution        = [self popup:resTitles tags:resTags];
	self.windowScale       = [self popup:@[ @"100%", @"150%", @"200%" ] tags:@[ @100, @150, @200 ]];
	self.scaling           = [self popup:@[ @"Sharp pixels", @"Smooth" ] tags:@[ @0, @1 ]];
	NSTabViewItem* display = [self
		   tab:@"Display"
		symbol:@"display"
		  rows:@[
			  @[ Label(@"Mode:"), self.mode ],
			  @[ Label(@"Resolution:"), self.resolution ],
			  @[ Label(@"Window size:"), self.windowScale ],
			  @[ Label(@"Scaling:"), self.scaling ],
			  @[
				  empty, Note(@"\"Fit the screen\" plays at your display's size (below the notch, "
							  @"if it has one), so the game fills it. "
							  @"Window size scales the window in window mode. "
							  @"Sharp pixels keeps the game's pixels crisp "
							  @"when it's scaled up.")
			  ],
		  ]];

	// Graphics
	self.viewDistance      = [self slider:256 max:512 ticks:9];
	self.viewDistanceValue = [NSTextField labelWithString:@""];
	NSArray* quality       = @[ @"High", @"Low" ];
	self.textureChr        = [self popup:quality tags:@[ @0, @1 ]];
	self.textureShape      = [self popup:quality tags:@[ @0, @1 ]];
	self.textureTerrain    = [self popup:quality tags:@[ @0, @1 ]];
	self.shadows           = [self checkbox:@"Character shadows"];
	self.systemCursor      = [self checkbox:@"Use the macOS cursor"];
	NSStackView* distance  = [NSStackView
        stackViewWithViews:@[ self.viewDistance, self.viewDistanceValue ]];
	NSTabViewItem* graphics    = [self
           tab:@"Graphics"
        symbol:@"paintbrush"
          rows:@[
              @[ Label(@"View distance:"), distance ],
              @[ Label(@"Character textures:"), self.textureChr ],
              @[ Label(@"Object textures:"), self.textureShape ],
              @[ Label(@"Terrain textures:"), self.textureTerrain ],
              @[ empty, self.shadows ],
              @[ empty, self.systemCursor ],
              @[
                  empty, Note(@"Low textures load at half size and use less memory. Without the "
									@"macOS cursor, the game draws its own.")
              ],
          ]];

	// Sound
	self.music                 = [self checkbox:@"Music"];
	self.soundEffects          = [self checkbox:@"Sound effects"];
	self.soundDistance         = [self slider:20 max:48 ticks:8];
	self.soundDistanceValue    = [NSTextField labelWithString:@""];
	NSStackView* range         = [NSStackView
        stackViewWithViews:@[ self.soundDistance, self.soundDistanceValue ]];
	NSTabViewItem* sound       = [self
           tab:@"Sound"
        symbol:@"speaker.wave.2"
          rows:@[
              @[ empty, self.music ],
              @[ empty, self.soundEffects ],
              @[ Label(@"Sound effect range:"), range ],
              @[ empty, Note(@"How far away you hear other characters and monsters.") ],
          ]];

	// Controls
	self.swipeTurns            = [self checkbox:@"Two-finger swipe left or right turns the camera"];
	self.swipeSpeed            = [self slider:25 max:400 ticks:16];
	self.swipeSpeedValue       = [NSTextField labelWithString:@""];
	self.swipeReversed         = [self checkbox:@"Reverse the swipe direction"];
	self.controlClick          = [self checkbox:@"Control-click acts as a right-click"];
	NSStackView* speed         = [NSStackView
        stackViewWithViews:@[ self.swipeSpeed, self.swipeSpeedValue ]];
	NSTabViewItem* controls    = [self
           tab:@"Controls"
        symbol:@"hand.point.up.left"
          rows:@[
              @[ empty, self.swipeTurns ],
              @[ Label(@"Turn speed:"), speed ],
              @[ empty, self.swipeReversed ],
              @[ empty, self.controlClick ],
              @[
                  empty,
                  Note(@"Two-finger swipe up or down zooms. Right-clicking an NPC talks to it, "
							 @"and dragging with the right button held turns the camera too. "
							 @"The game uses Control with clicks itself, which this option replaces.")
              ],
          ]];

	// Performance
	self.vsync                 = [self checkbox:@"VSync (draw in step with the display)"];
	self.maxFps                = [self popup:@[ @"No limit", @"120 fps", @"60 fps", @"30 fps" ]
                         tags:@[ @0, @120, @60, @30 ]];
	self.backgroundFps         = [self popup:@[ @"Full speed", @"30 fps", @"10 fps" ]
                                tags:@[ @0, @30, @10 ]];
	self.keepAwake             = [self checkbox:@"Keep the display awake while playing"];
	NSTabViewItem* performance = [self
		   tab:@"Performance"
		symbol:@"gauge.with.dots.needle.67percent"
		  rows:@[
			  @[ empty, self.vsync ],
			  @[ Label(@"Frame rate limit:"), self.maxFps ],
			  @[ Label(@"In the background:"), self.backgroundFps ],
			  @[ empty, self.keepAwake ],
			  @[
				  empty,
				  Note(@"A lower frame rate limit saves battery. In the background means while "
					   @"another app is in front.")
			  ],
		  ]];

	self.tabs                  = [[NSTabViewController alloc] init];
	self.tabs.tabStyle         = NSTabViewControllerTabStyleToolbar;
	for (NSTabViewItem* item in @[ display, graphics, sound, controls, performance ])
		[self.tabs addTabViewItem:item];

	// The window: the tabs, then a footer.
	NSTextField* footnote                = Note(self.afterQuit
													? @"The game is closed. Start it again to use these settings."
													: @"Changes take effect the next time the game starts.");
	NSMutableArray<NSView*>* footerViews = [NSMutableArray arrayWithObject:footnote];
	[footerViews addObject:[[NSView alloc] initWithFrame:NSZeroRect]];
	if (self.afterQuit)
	{
		NSButton* quit  = [NSButton buttonWithTitle:@"Quit" target:self action:@selector(quit:)];
		NSButton* start = [NSButton buttonWithTitle:@"Start Game"
											 target:self
											 action:@selector(start:)];
		start.keyEquivalent = @"\r";
		[footerViews addObjectsFromArray:@[ quit, start ]];
	}
	else
	{
		[footerViews addObject:[NSButton buttonWithTitle:@"Restart Game"
												  target:self
												  action:@selector(restart:)]];
	}
	NSStackView* footer = [NSStackView stackViewWithViews:footerViews];
	footer.edgeInsets   = NSEdgeInsetsMake(10, 20, 16, 20);
	[footer setHuggingPriority:NSLayoutPriorityDefaultLow
				forOrientation:NSLayoutConstraintOrientationHorizontal];

	NSStackView* content = [NSStackView stackViewWithViews:@[ self.tabs.view, footer ]];
	content.orientation  = NSUserInterfaceLayoutOrientationVertical;
	content.spacing      = 0;
	content.alignment    = NSLayoutAttributeLeading;
	[footer.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;

	NSViewController* root         = [[NSViewController alloc] init];
	root.view                      = content;

	self.window                    = [NSWindow windowWithContentViewController:root];
	self.window.title              = @"Settings";
	self.window.styleMask          = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
	self.window.toolbarStyle       = NSWindowToolbarStyleUnified;
	self.window.delegate           = self;
	self.window.releasedWhenClosed = NO;
	// Above the game, which in fullscreen sits above the menu bar.
	self.window.level              = NSMainMenuWindowLevel + 2;
	[self.window center];
}

- (void)load
{
	const GameOptions& o = self.options;
	[self.mode selectItemWithTag:o.bFullscreen ? 0 : 1];

	NSInteger resTag = -2;
	if (o.iWidth == self.fitWidth && o.iHeight == self.fitHeight)
		resTag = -1;
	for (size_t i = 0; i < Resolutions.size(); i++)
		if (resTag == -2 && Resolutions[i].first == o.iWidth && Resolutions[i].second == o.iHeight)
			resTag = static_cast<NSInteger>(i);
	if (resTag == -2)
	{
		// Something else in Option.ini: keep it as a choice.
		[self.resolution
			addItemWithTitle:[NSString stringWithFormat:@"%d × %d", o.iWidth, o.iHeight]];
		self.resolution.lastItem.tag = 100;
		resTag                       = 100;
	}
	[self.resolution selectItemWithTag:resTag];

	if (![self.windowScale selectItemWithTag:o.iWindowScale])
		[self.windowScale selectItemWithTag:100];
	[self.scaling selectItemWithTag:o.bSmoothScaling ? 1 : 0];

	self.viewDistance.integerValue = o.iViewDistance;
	[self.textureChr selectItemWithTag:o.iTextureChr];
	[self.textureShape selectItemWithTag:o.iTextureShape];
	[self.textureTerrain selectItemWithTag:o.iTextureTerrain];
	self.shadows.state      = o.bShadows ? NSControlStateValueOn : NSControlStateValueOff;
	self.systemCursor.state = o.bSystemCursor ? NSControlStateValueOn : NSControlStateValueOff;

	self.music.state        = o.bMusic ? NSControlStateValueOn : NSControlStateValueOff;
	self.soundEffects.state = o.bSoundEffects ? NSControlStateValueOn : NSControlStateValueOff;
	self.soundDistance.integerValue = o.iSoundDistance;

	self.swipeTurns.state        = o.bSwipeTurns ? NSControlStateValueOn : NSControlStateValueOff;
	self.swipeSpeed.integerValue = o.iSwipeSpeed;
	self.swipeReversed.state = o.bSwipeReversed ? NSControlStateValueOn : NSControlStateValueOff;
	self.controlClick.state  = o.bControlClick ? NSControlStateValueOn : NSControlStateValueOff;

	self.vsync.state         = o.bVSync ? NSControlStateValueOn : NSControlStateValueOff;
	if (![self.maxFps selectItemWithTag:o.iMaxFps])
		[self.maxFps selectItemWithTag:0];
	if (![self.backgroundFps selectItemWithTag:o.iBackgroundFps])
		[self.backgroundFps selectItemWithTag:10];
	self.keepAwake.state = o.bKeepAwake ? NSControlStateValueOn : NSControlStateValueOff;
	[self updateDependents];
}

// Labels next to sliders, and what only applies in some modes.
- (void)updateDependents
{
	self.viewDistanceValue.stringValue  = [NSString
        stringWithFormat:@"%ld", self.viewDistance.integerValue];
	self.soundDistanceValue.stringValue = [NSString
		stringWithFormat:@"%ld", self.soundDistance.integerValue];
	self.swipeSpeedValue.stringValue    = [NSString
        stringWithFormat:@"%ld%%", self.swipeSpeed.integerValue];
	self.windowScale.enabled            = self.mode.selectedTag == 1;
	BOOL swipe                          = self.swipeTurns.state == NSControlStateValueOn;
	self.swipeSpeed.enabled             = swipe;
	self.swipeReversed.enabled          = swipe;
}

- (void)changed:(id)sender
{
	GameOptions o = self.options;
	o.bFullscreen = self.mode.selectedTag == 0;
	NSInteger res = self.resolution.selectedTag;
	if (res == -1)
	{
		o.iWidth  = self.fitWidth;
		o.iHeight = self.fitHeight;
	}
	else if (res >= 0 && res < static_cast<NSInteger>(Resolutions.size()))
	{
		o.iWidth  = Resolutions[static_cast<size_t>(res)].first;
		o.iHeight = Resolutions[static_cast<size_t>(res)].second;
	}
	o.iWindowScale    = static_cast<int>(self.windowScale.selectedTag);
	o.bSmoothScaling  = self.scaling.selectedTag == 1;
	o.iViewDistance   = static_cast<int>(self.viewDistance.integerValue);
	o.iTextureChr     = static_cast<int>(self.textureChr.selectedTag);
	o.iTextureShape   = static_cast<int>(self.textureShape.selectedTag);
	o.iTextureTerrain = static_cast<int>(self.textureTerrain.selectedTag);
	o.bShadows        = self.shadows.state == NSControlStateValueOn;
	o.bSystemCursor   = self.systemCursor.state == NSControlStateValueOn;
	o.bMusic          = self.music.state == NSControlStateValueOn;
	o.bSoundEffects   = self.soundEffects.state == NSControlStateValueOn;
	o.iSoundDistance  = static_cast<int>(self.soundDistance.integerValue);
	o.bSwipeTurns     = self.swipeTurns.state == NSControlStateValueOn;
	o.iSwipeSpeed     = static_cast<int>(self.swipeSpeed.integerValue);
	o.bSwipeReversed  = self.swipeReversed.state == NSControlStateValueOn;
	o.bControlClick   = self.controlClick.state == NSControlStateValueOn;
	o.bVSync          = self.vsync.state == NSControlStateValueOn;
	o.iMaxFps         = static_cast<int>(self.maxFps.selectedTag);
	o.iBackgroundFps  = static_cast<int>(self.backgroundFps.selectedTag);
	o.bKeepAwake      = self.keepAwake.state == NSControlStateValueOn;
	self.options      = o;
	[self updateDependents];

	if (self.hooks.OnChange)
		self.hooks.OnChange(o);
}

- (void)restart:(id)sender
{
	[self.window close];
	if (self.hooks.OnRestart)
		self.hooks.OnRestart();
}

- (void)start:(id)sender
{
	self.startGame = YES;
	[NSApp stopModal];
}

- (void)quit:(id)sender
{
	[NSApp stopModal];
}

- (void)windowWillClose:(NSNotification*)notification
{
	if (self.afterQuit)
		[NSApp stopModal];
}

@end

namespace Platform::macOS::Cocoa
{

namespace
{

KOOptionsController* s_controller = nil;

KOOptionsController* MakeController(const GameOptions& options, int iFitWidth, int iFitHeight,
	bool bAfterQuit, const OptionsWindowHooks& hooks)
{
	KOOptionsController* controller = [[KOOptionsController alloc] init];
	controller.options              = options;
	controller.hooks                = hooks;
	controller.fitWidth             = iFitWidth;
	controller.fitHeight            = iFitHeight;
	controller.afterQuit            = bAfterQuit;
	[controller build];
	[controller load];
	return controller;
}

} // namespace

bool ShowOptionsWindow(const GameOptions& options, int iFitWidth, int iFitHeight, bool bAfterQuit,
	const OptionsWindowHooks& hooks)
{
	@autoreleasepool
	{
		if (!bAfterQuit && s_controller != nil && s_controller.window.visible)
		{
			[s_controller.window makeKeyAndOrderFront:nil];
			return false;
		}

		s_controller = MakeController(options, iFitWidth, iFitHeight, bAfterQuit, hooks);
		[s_controller.window makeKeyAndOrderFront:nil];
		[NSApp activate];
		if (!bAfterQuit)
			return false;

		[NSApp runModalForWindow:s_controller.window];
		bool bStart = s_controller.startGame;
		[s_controller.window orderOut:nil];
		s_controller = nil;
		return bStart;
	}
}

bool SnapshotOptionsWindow(
	const GameOptions& options, int iFitWidth, int iFitHeight, const std::string& szPrefix)
{
	@autoreleasepool
	{
		[NSApplication sharedApplication];
		[NSApp
			setActivationPolicy:NSApplicationActivationPolicyAccessory]; // no Dock icon, no focus

		// Views only lay out and draw once the window is on screen: show it (without activating
		// the app), let each tab settle, and capture the whole window including its toolbar.
		KOOptionsController* controller = MakeController(options, iFitWidth, iFitHeight, false, {});
		[controller.window orderFrontRegardless];
		bool bOk = true;
		for (NSInteger i = 0; i < static_cast<NSInteger>(controller.tabs.tabViewItems.count); i++)
		{
			controller.tabs.selectedTabViewItemIndex = i;
			[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.4]];
			NSView* frame          = controller.window.contentView.superview;
			NSBitmapImageRep* bits = [frame bitmapImageRepForCachingDisplayInRect:frame.bounds];
			[frame cacheDisplayInRect:frame.bounds toBitmapImageRep:bits];
			NSData* png = [bits representationUsingType:NSBitmapImageFileTypePNG properties:@ {}];
			NSString* path = [NSString
				stringWithFormat:@"%s-%ld.png", szPrefix.c_str(), static_cast<long>(i)];
			bOk            = [png writeToFile:path atomically:YES] && bOk;
		}
		[controller.window close];
		return bOk;
	}
}

} // namespace Platform::macOS::Cocoa

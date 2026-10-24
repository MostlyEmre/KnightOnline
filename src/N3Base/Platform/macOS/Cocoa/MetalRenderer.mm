#include "MetalRenderer.h"

#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <tuple>

using namespace Platform::macOS::Cocoa;

namespace
{

// The fixed-function pipeline in MSL. Matches Direct3D 9 as WARP (the reference rasterizer the
// port is compared against) implements it, including three WARP behaviours: an unbound texture
// reads as white, point sampling picks texel floor(uv * size), and alpha test compares the
// unquantised alpha.
const char* kShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Light { float4 diffuse, specular, ambient, position, direction, params, spot; uint4 info; };
struct Stage { uint4 a, b, c, d; };
struct U {
	float4x4 world, view, proj, normalMat;
	float4 viewport, viewportInv;
	uint4 v0, v1, v2, v3, v4;
	float4 ambient, matDiffuse, matAmbient, matSpecular, matEmissive;
	float4 p0, p1;
	float4 fogColor, tfactor, lodBias;
	Light lights[8];
	Stage stages[3];
};

struct VOut {
	float4 pos [[position]];
	float psize [[point_size]];
	float4 diffuse;
	float4 specular;
	float2 uv0;
	float2 uv1;
	float fogDist;
};

constant uint FVF_XYZRHW = 0x004, FVF_POSITION_MASK = 0x400E, FVF_NORMAL = 0x010, FVF_PSIZE = 0x020,
	FVF_DIFFUSE = 0x040, FVF_SPECULAR = 0x080;

static float4 unpackColor(uint c)
{
	return float4((c >> 16) & 255u, (c >> 8) & 255u, c & 255u, c >> 24) / 255.0;
}

static float f(device const uint* vb, uint i)
{
	return as_type<float>(vb[i]);
}

static float4 materialSource(uint source, float4 material, float4 vdiff, float4 vspec)
{
	return source == 1 ? vdiff : (source == 2 ? vspec : material);
}

vertex VOut vs(uint vid [[vertex_id]], device const uint* vb [[buffer(0)]], constant U& u [[buffer(1)]])
{
	VOut o;
	uint base = vid * u.v0.y;
	uint fvf = u.v0.x;
	uint texCount = (fvf >> 8) & 0xFu;
	float4 vdiff = (fvf & FVF_DIFFUSE) != 0 ? unpackColor(vb[base + u.v0.w]) : float4(1.0);
	float4 vspec = (fvf & FVF_SPECULAR) != 0 ? unpackColor(vb[base + u.v1.x]) : float4(0.0);
	o.uv0 = texCount >= 1 ? float2(f(vb, base + u.v1.y), f(vb, base + u.v1.y + 1)) : float2(0.0);
	o.uv1 = texCount >= 2 ? float2(f(vb, base + u.v1.z), f(vb, base + u.v1.z + 1)) : o.uv0;
	o.psize = (fvf & FVF_PSIZE) != 0 ? f(vb, base + u.v1.w) : u.p1.x;

	if ((fvf & FVF_POSITION_MASK) == FVF_XYZRHW)
	{
		// Screen-space vertices. D3D9 pixel centres sit on integer coordinates, Metal's on +0.5.
		float x = f(vb, base) + 0.5, y = f(vb, base + 1) + 0.5, z = f(vb, base + 2), rhw = f(vb, base + 3);
		float w = rhw != 0.0 ? 1.0 / rhw : 1.0;
		float nx = (x - u.viewport.x) * u.viewportInv.x * 2.0 - 1.0;
		float ny = 1.0 - (y - u.viewport.y) * u.viewportInv.y * 2.0;
		o.pos = float4(nx * w, ny * w, z * w, w);
		o.fogDist = w;
		o.diffuse = vdiff;
		o.specular = vspec;
		return o;
	}

	float4 p = float4(f(vb, base), f(vb, base + 1), f(vb, base + 2), 1.0);
	float4 vp = u.view * (u.world * p);
	float4 cp = u.proj * vp;
	// The same half-pixel shift for transformed geometry, applied in clip space.
	cp.x += cp.w * u.viewportInv.x;
	cp.y -= cp.w * u.viewportInv.y;
	o.pos = cp;
	o.fogDist = u.p1.y == 1.0 ? length(vp.xyz) : (u.p1.y == 2.0 ? vp.z : cp.w);

	if (u.v2.x == 0) // lighting off
	{
		o.diffuse = vdiff;
		o.specular = vspec;
		return o;
	}

	float3 n = (fvf & FVF_NORMAL) != 0 ? float3(f(vb, base + u.v0.z), f(vb, base + u.v0.z + 1), f(vb, base + u.v0.z + 2)) : float3(0.0);
	float3 N = (u.normalMat * float4(n, 0.0)).xyz;
	if (u.v2.w != 0 && dot(N, N) > 0.0)
		N = normalize(N);

	float3 V = vp.xyz;
	float4 cd = materialSource(u.v4.x, u.matDiffuse, vdiff, vspec);
	float4 ca = materialSource(u.v4.y, u.matAmbient, vdiff, vspec);
	float4 cs = materialSource(u.v4.z, u.matSpecular, vdiff, vspec);
	float4 ce = materialSource(u.v4.w, u.matEmissive, vdiff, vspec);

	float3 amb = float3(0.0), dif = float3(0.0), spe = float3(0.0);
	for (int i = 0; i < 8; i++)
	{
		Light L = u.lights[i];
		if (L.info.x == 0)
			continue;

		float3 ldir;
		float atten = 1.0;
		if (L.info.x == 3) // directional
		{
			ldir = normalize(-L.direction.xyz);
		}
		else
		{
			float3 d = L.position.xyz - V;
			float dist = length(d);
			if (dist > L.params.x)
				continue;
			ldir = dist > 0.0 ? d / dist : float3(0.0, 0.0, 1.0);
			atten = 1.0 / (L.params.y + L.params.z * dist + L.params.w * dist * dist);
			if (L.info.x == 2) // spot
			{
				float rho = dot(normalize(L.direction.xyz), -ldir);
				float spot = rho > L.spot.y ? 1.0 : (rho <= L.spot.z ? 0.0 : pow((rho - L.spot.z) / (L.spot.y - L.spot.z), L.spot.x));
				atten *= spot;
			}
		}

		amb += L.ambient.rgb * atten;
		float ndl = dot(N, ldir);
		if (ndl > 0.0)
		{
			dif += L.diffuse.rgb * ndl * atten;
			if (u.v2.y != 0)
			{
				float3 h = u.v2.z != 0 ? normalize(normalize(-V) + ldir) : normalize(float3(0.0, 0.0, 1.0) + ldir);
				float ndh = dot(N, h);
				if (ndh > 0.0)
					spe += L.specular.rgb * pow(ndh, u.p0.x) * atten;
			}
		}
	}

	o.diffuse = float4(saturate(ce.rgb + ca.rgb * (u.ambient.rgb + amb) + cd.rgb * dif), saturate(cd.a));
	o.specular = float4(saturate(cs.rgb * spe), 0.0);
	return o;
}

static float4 argValue(uint arg, float4 diffuse, float4 current, float4 tex, float4 tfactor, float4 specular)
{
	float4 v;
	switch (arg & 0xFu)
	{
		case 0: v = diffuse; break;
		case 1: v = current; break;
		case 2: v = tex; break;
		case 3: v = tfactor; break;
		case 4: v = specular; break;
		case 5: v = float4(0.0); break; // D3DTA_TEMP (never written here)
		default: v = float4(1.0); break;
	}
	if ((arg & 0x20u) != 0) // D3DTA_ALPHAREPLICATE
		v = v.aaaa;
	if ((arg & 0x10u) != 0) // D3DTA_COMPLEMENT
		v = 1.0 - v;
	return v;
}

static float4 combine(uint op, float4 a0, float4 a1, float4 a2, float4 diffuse, float4 tex, float4 tfactor, float4 current)
{
	switch (op)
	{
		case 2: return a1;                                       // SELECTARG1
		case 3: return a2;                                       // SELECTARG2
		case 4: return a1 * a2;                                  // MODULATE
		case 5: return a1 * a2 * 2.0;                            // MODULATE2X
		case 6: return a1 * a2 * 4.0;                            // MODULATE4X
		case 7: return a1 + a2;                                  // ADD
		case 8: return a1 + a2 - 0.5;                            // ADDSIGNED
		case 9: return (a1 + a2 - 0.5) * 2.0;                    // ADDSIGNED2X
		case 10: return a1 - a2;                                 // SUBTRACT
		case 11: return a1 + a2 - a1 * a2;                       // ADDSMOOTH
		case 12: return mix(a2, a1, diffuse.a);                  // BLENDDIFFUSEALPHA
		case 13: return mix(a2, a1, tex.a);                      // BLENDTEXTUREALPHA
		case 14: return mix(a2, a1, tfactor.a);                  // BLENDFACTORALPHA
		case 15: return a1 + a2 * (1.0 - tex.a);                 // BLENDTEXTUREALPHAPM
		case 16: return mix(a2, a1, current.a);                  // BLENDCURRENTALPHA
		case 18: return float4(a1.rgb + a1.a * a2.rgb, a1.a);    // MODULATEALPHA_ADDCOLOR
		case 19: return float4(a1.rgb * a2.rgb + a1.a, a1.a);    // MODULATECOLOR_ADDALPHA
		case 20: return float4((1.0 - a1.a) * a2.rgb + a1.rgb, a1.a); // MODULATEINVALPHA_ADDCOLOR
		case 21: return float4((1.0 - a1.rgb) * a2.rgb + a1.a, a1.a); // MODULATEINVCOLOR_ADDALPHA
		case 24: return float4(saturate(4.0 * dot(a1.rgb - 0.5, a2.rgb - 0.5))); // DOTPRODUCT3
		case 25: return a0 + a1 * a2;                            // MULTIPLYADD
		case 26: return mix(a2, a1, a0);                         // LERP
		default: return a1;
	}
}

static float4 sampleStage(constant U& u, uint i, VOut in, texture2d<float> t, sampler s)
{
	Stage st = u.stages[i];
	if (st.b.z == 0)
		return float4(1.0);

	float2 uv = (st.b.w & 0xFFFFu) == 0 ? in.uv0 : in.uv1;
	if (st.c.z != 0) // D3DTADDRESS_BORDER, with any border colour
	{
		bool outside = ((st.c.z & 1u) != 0 && (uv.x < 0.0 || uv.x > 1.0)) || ((st.c.z & 2u) != 0 && (uv.y < 0.0 || uv.y > 1.0));
		if (outside)
			return unpackColor(st.c.w);
	}

	if (st.c.y != 0)
	{
		// WARP picks texel floor(uv * size) even when the interpolated coordinate lands exactly on
		// a texel edge; Apple GPUs can land a hair below it. Snap to the centre of WARP's texel.
		float2 size = float2(t.get_width(), t.get_height());
		uv = (floor(uv * size + 1.0 / 256.0) + 0.5) / size;
	}

	return st.c.x != 0 ? t.sample(s, uv, bias(u.lodBias[i])) : t.sample(s, uv);
}

static bool alphaTest(uint func, float a, float ref)
{
	switch (func)
	{
		case 1: return false;
		case 2: return a < ref;
		case 3: return a == ref;
		case 4: return a <= ref;
		case 5: return a > ref;
		case 6: return a != ref;
		case 7: return a >= ref;
		default: return true;
	}
}

fragment float4 fs(VOut in [[stage_in]], constant U& u [[buffer(1)]],
	texture2d<float> t0 [[texture(0)]], texture2d<float> t1 [[texture(1)]], texture2d<float> t2 [[texture(2)]],
	sampler s0 [[sampler(0)]], sampler s1 [[sampler(1)]], sampler s2 [[sampler(2)]])
{
	float4 diffuse = in.diffuse;
	float4 specular = in.specular;
	float4 current = diffuse;
	for (uint i = 0; i < 3; i++)
	{
		Stage st = u.stages[i];
		if (st.a.x == 1) // D3DTOP_DISABLE ends the cascade
			break;

		float4 tex = i == 0 ? sampleStage(u, 0, in, t0, s0) : (i == 1 ? sampleStage(u, 1, in, t1, s1) : sampleStage(u, 2, in, t2, s2));
		float4 c0 = argValue(st.d.x, diffuse, current, tex, u.tfactor, specular);
		float4 c1 = argValue(st.a.y, diffuse, current, tex, u.tfactor, specular);
		float4 c2 = argValue(st.a.z, diffuse, current, tex, u.tfactor, specular);
		float3 rgb = saturate(combine(st.a.x, c0, c1, c2, diffuse, tex, u.tfactor, current)).rgb;

		float a = current.a;
		if (st.a.w != 1)
		{
			float4 a0 = argValue(st.d.y, diffuse, current, tex, u.tfactor, specular);
			float4 a1 = argValue(st.b.x, diffuse, current, tex, u.tfactor, specular);
			float4 a2 = argValue(st.b.y, diffuse, current, tex, u.tfactor, specular);
			a = saturate(combine(st.a.w, a0, a1, a2, diffuse, tex, u.tfactor, current)).a;
		}

		current = float4(rgb, a);
	}

	if (u.v2.y != 0) // D3DRS_SPECULARENABLE
		current.rgb = saturate(current.rgb + specular.rgb);

	if (u.v3.z != 0 && !alphaTest(u.v3.z, saturate(current.a), float(u.v3.w) / 255.0))
		discard_fragment();

	if (u.v3.x != 0) // fog
	{
		float d = in.fogDist;
		float fogf = 1.0;
		if (u.v3.y == 1)
			fogf = exp(-d * u.p0.y);
		else if (u.v3.y == 2)
			fogf = exp(-(d * u.p0.y) * (d * u.p0.y));
		else if (u.v3.y == 3)
			fogf = (u.p0.w - d) / (u.p0.w - u.p0.z);
		current.rgb = mix(u.fogColor.rgb, current.rgb, saturate(fogf));
	}

	return current;
}

struct ClearU { float4 rect; float4 color; float4 depth; };

vertex float4 clear_vs(uint vid [[vertex_id]], constant ClearU& c [[buffer(0)]])
{
	float2 corner = float2((vid & 1u) != 0 ? c.rect.z : c.rect.x, (vid & 2u) != 0 ? c.rect.w : c.rect.y);
	return float4(corner, c.depth.x, 1.0);
}

fragment float4 clear_fs(constant ClearU& c [[buffer(0)]])
{
	return c.color;
}
)MSL";

struct ClearUniforms
{
	float rect[4]; // NDC left, top, right, bottom
	float color[4];
	float depth[4];
};

constexpr int FramesInFlight = 3;

struct TextureObject
{
	id<MTLTexture> texture;
	uint32_t width         = 0;
	uint32_t height        = 0;
	uint32_t levels        = 0;
	TextureFormat format   = TextureFormat::BGRA8;
	uint64_t lastUseSerial = 0; // frame that last bound it (0 = never)
};

MTLPixelFormat PixelFormat(TextureFormat format)
{
	switch (format)
	{
		case TextureFormat::BC1:
			return MTLPixelFormatBC1_RGBA;
		case TextureFormat::BC2:
			return MTLPixelFormatBC2_RGBA;
		case TextureFormat::BC3:
			return MTLPixelFormatBC3_RGBA;
		default:
			return MTLPixelFormatBGRA8Unorm;
	}
}

size_t RowBytes(TextureFormat format, uint32_t width)
{
	switch (format)
	{
		case TextureFormat::BC1:
			return ((width + 3) / 4) * 8;
		case TextureFormat::BC2:
		case TextureFormat::BC3:
			return ((width + 3) / 4) * 16;
		default:
			return static_cast<size_t>(width) * 4;
	}
}

size_t LevelRows(TextureFormat format, uint32_t height)
{
	return format == TextureFormat::BGRA8 ? height : (height + 3) / 4;
}

MTLBlendFactor BlendFactor(uint32_t blend)
{
	switch (blend)
	{
		case 1:
			return MTLBlendFactorZero;
		case 2:
			return MTLBlendFactorOne;
		case 3:
			return MTLBlendFactorSourceColor;
		case 4:
			return MTLBlendFactorOneMinusSourceColor;
		case 5:
			return MTLBlendFactorSourceAlpha;
		case 6:
			return MTLBlendFactorOneMinusSourceAlpha;
		case 7:
			return MTLBlendFactorDestinationAlpha;
		case 8:
			return MTLBlendFactorOneMinusDestinationAlpha;
		case 9:
			return MTLBlendFactorDestinationColor;
		case 10:
			return MTLBlendFactorOneMinusDestinationColor;
		case 11:
			return MTLBlendFactorSourceAlphaSaturated;
		default:
			return MTLBlendFactorOne;
	}
}

MTLBlendOperation BlendOperation(uint32_t op)
{
	switch (op)
	{
		case 2:
			return MTLBlendOperationSubtract;
		case 3:
			return MTLBlendOperationReverseSubtract;
		case 4:
			return MTLBlendOperationMin;
		case 5:
			return MTLBlendOperationMax;
		default:
			return MTLBlendOperationAdd;
	}
}

MTLCompareFunction CompareFunction(uint32_t func)
{
	switch (func)
	{
		case 1:
			return MTLCompareFunctionNever;
		case 2:
			return MTLCompareFunctionLess;
		case 3:
			return MTLCompareFunctionEqual;
		case 4:
			return MTLCompareFunctionLessEqual;
		case 5:
			return MTLCompareFunctionGreater;
		case 6:
			return MTLCompareFunctionNotEqual;
		case 7:
			return MTLCompareFunctionGreaterEqual;
		default:
			return MTLCompareFunctionAlways;
	}
}

MTLSamplerMinMagFilter MinMagFilter(uint32_t filter)
{
	return filter == 1 /* D3DTEXF_POINT */ || filter == 0 ? MTLSamplerMinMagFilterNearest
														  : MTLSamplerMinMagFilterLinear;
}

MTLSamplerAddressMode AddressMode(uint32_t address)
{
	switch (address)
	{
		case 2:
			return MTLSamplerAddressModeMirrorRepeat;
		case 3:
			return MTLSamplerAddressModeClampToEdge;
		case 4:
			return MTLSamplerAddressModeClampToEdge; // border colour handled in the shader
		case 5:
			return MTLSamplerAddressModeMirrorClampToEdge;
		default:
			return MTLSamplerAddressModeRepeat;
	}
}

class MetalRendererImpl : public MetalRenderer
{
public:
	bool Init(CAMetalLayer* layer, int width, int height, bool vsync)
	{
		_layer  = layer;
		_device = layer.device != nil ? layer.device : MTLCreateSystemDefaultDevice();
		if (_device == nil)
			return false;

		_layer.device               = _device;
		_layer.pixelFormat          = MTLPixelFormatBGRA8Unorm;
		_layer.framebufferOnly      = NO;
		_layer.opaque               = YES;
		_layer.maximumDrawableCount = 3;
		_queue                      = [_device newCommandQueue];
		_frameSemaphore             = dispatch_semaphore_create(FramesInFlight);

		MTLCompileOptions* options  = [MTLCompileOptions new];
		options.mathMode            = MTLMathModeSafe;
		NSError* error              = nil;
		_library = [_device newLibraryWithSource:@(kShaderSource) options:options error:&error];
		if (_library == nil)
		{
			NSLog(@"Metal: shader compilation failed: %@", error.localizedDescription);
			return false;
		}

		_vs                         = [_library newFunctionWithName:@"vs"];
		_fs                         = [_library newFunctionWithName:@"fs"];
		_clearVs                    = [_library newFunctionWithName:@"clear_vs"];
		_clearFs                    = [_library newFunctionWithName:@"clear_fs"];

		// What an unbound stage samples (the shader returns white for it anyway).
		MTLTextureDescriptor* white = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
										 width:1
										height:1
									 mipmapped:NO];
		_whiteTexture               = [_device newTextureWithDescriptor:white];
		const uint32_t pixel        = 0xFFFFFFFF;
		[_whiteTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
						 mipmapLevel:0
						   withBytes:&pixel
						 bytesPerRow:4];

		for (auto& ring : _rings)
			ring.buffer = [_device newBufferWithLength:4 * 1024 * 1024
											   options:MTLResourceStorageModeShared];

		Resize(width, height, vsync);
		return true;
	}

	void Resize(int width, int height, bool vsync) override
	{
		_width                      = std::max(1, width);
		_height                     = std::max(1, height);
		_layer.drawableSize         = CGSizeMake(_width, _height);
		_layer.displaySyncEnabled   = vsync;

		MTLTextureDescriptor* depth = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
										 width:_width
										height:_height
									 mipmapped:NO];
		depth.usage                 = MTLTextureUsageRenderTarget;
		depth.storageMode           = MTLStorageModePrivate;
		_depthTexture               = [_device newTextureWithDescriptor:depth];
	}

	int Width() const override
	{
		return _width;
	}

	int Height() const override
	{
		return _height;
	}

	void* CreateTexture(
		uint32_t width, uint32_t height, uint32_t levels, TextureFormat format) override
	{
		auto* object    = new TextureObject();
		object->width   = std::max(1u, width);
		object->height  = std::max(1u, height);
		object->levels  = std::max(1u, levels);
		object->format  = format;
		object->texture = NewTexture(*object);
		if (object->texture == nil)
		{
			delete object;
			return nullptr;
		}
		return object;
	}

	void UploadTexture(void* texture, uint32_t level, const void* data, size_t rowBytes) override
	{
		auto* object = static_cast<TextureObject*>(texture);
		if (object == nullptr || level >= object->levels)
			return;

		// A frame the GPU hasn't finished still reads this texture: give it a fresh copy instead,
		// so those draws keep seeing the old contents (D3D9 semantics) without waiting.
		if (object->lastUseSerial > _completedSerial.load())
			Rename(*object);

		uint32_t w = std::max(1u, object->width >> level),
				 h = std::max(1u, object->height >> level);
		[object->texture replaceRegion:MTLRegionMake2D(0, 0, w, h)
						   mipmapLevel:level
							 withBytes:data
						   bytesPerRow:rowBytes];
	}

	void DestroyTexture(void* texture) override
	{
		delete static_cast<TextureObject*>(
			texture); // command buffers still using it hold their own reference
	}

	void Clear(const ClearDesc& desc) override
	{
		if (!desc.color && !desc.depth)
			return;

		int left = std::clamp(desc.rect[0], 0, _width), top = std::clamp(desc.rect[1], 0, _height);
		int right  = std::clamp(desc.rect[2], 0, _width),
			bottom = std::clamp(desc.rect[3], 0, _height);
		if (right <= left || bottom <= top || !BeginFrame())
			return;

		ClearUniforms c {};
		c.rect[0] = -1.0f;
		c.rect[1] = 1.0f;
		c.rect[2] = 1.0f;
		c.rect[3] = -1.0f;
		std::memcpy(c.color, desc.rgba, sizeof(c.color));
		c.depth[0] = std::clamp(desc.z, 0.0f, 1.0f);

		SetPipeline(ClearPipeline(desc.color));
		SetDepthState(desc.depth ? DepthState(true, true, 8) : DepthState(false, false, 8));
		[_encoder setViewport:(MTLViewport) { 0, 0, (double) _width, (double) _height, 0, 1 }];
		[_encoder setScissorRect:(MTLScissorRect) { (NSUInteger) left, (NSUInteger) top,
									 (NSUInteger) (right - left), (NSUInteger) (bottom - top) }];
		[_encoder setCullMode:MTLCullModeNone];
		[_encoder setTriangleFillMode:MTLTriangleFillModeFill];
		_lastCull = -1;
		_lastFill = -1;
		[_encoder setVertexBytes:&c length:sizeof(c) atIndex:0];
		[_encoder setFragmentBytes:&c length:sizeof(c) atIndex:0];
		[_encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	}

	void Draw(const DrawDesc& desc) override
	{
		if (desc.vertices == nullptr || desc.vertexBytes == 0 || !BeginFrame())
			return;

		// Viewport and scissor (Metal wants the scissor inside the target).
		double vx = desc.viewport[0], vy = desc.viewport[1], vw = desc.viewport[2],
			   vh = desc.viewport[3];
		if (vw <= 0 || vh <= 0)
			return;

		int left = static_cast<int>(vx), top = static_cast<int>(vy);
		int right = static_cast<int>(vx + vw), bottom = static_cast<int>(vy + vh);
		if (desc.scissor)
		{
			left   = std::max(left, desc.scissorRect[0]);
			top    = std::max(top, desc.scissorRect[1]);
			right  = std::min(right, desc.scissorRect[2]);
			bottom = std::min(bottom, desc.scissorRect[3]);
		}
		left   = std::clamp(left, 0, _width);
		top    = std::clamp(top, 0, _height);
		right  = std::clamp(right, 0, _width);
		bottom = std::clamp(bottom, 0, _height);
		if (right <= left || bottom <= top)
			return;

		SetPipeline(DrawPipeline(desc.blend, desc.srcBlend, desc.destBlend, desc.blendOp));
		SetDepthState(DepthState(desc.depthTest, desc.depthWrite, desc.depthFunc));
		[_encoder setViewport:(MTLViewport) { vx, vy, vw, vh, desc.viewport[4], desc.viewport[5] }];
		[_encoder setScissorRect:(MTLScissorRect) { (NSUInteger) left, (NSUInteger) top,
									 (NSUInteger) (right - left), (NSUInteger) (bottom - top) }];

		int cull = static_cast<int>(desc.cullMode);
		if (cull != _lastCull)
		{
			// D3DCULL_CCW culls counter-clockwise triangles (screen space, y down).
			[_encoder setFrontFacingWinding:MTLWindingClockwise];
			[_encoder setCullMode:cull == 1 ? MTLCullModeNone
											: (cull == 3 ? MTLCullModeBack : MTLCullModeFront)];
			_lastCull = cull;
		}

		int fill = desc.wireframe ? 1 : 0;
		if (fill != _lastFill)
		{
			[_encoder setTriangleFillMode:desc.wireframe ? MTLTriangleFillModeLines
														 : MTLTriangleFillModeFill];
			_lastFill = fill;
		}

		for (NSUInteger i = 0; i < 3; i++)
		{
			auto* object = static_cast<TextureObject*>(desc.textures[i]);
			if (object != nullptr)
				object->lastUseSerial = _frameSerial;
			[_encoder setFragmentTexture:object != nullptr ? object->texture : _whiteTexture
								 atIndex:i];
			[_encoder setFragmentSamplerState:SamplerState(desc.samplers[i]) atIndex:i];
		}

		NSUInteger vertexOffset    = 0;
		id<MTLBuffer> vertexBuffer = Allocate(desc.vertices, desc.vertexBytes, vertexOffset);
		[_encoder setVertexBuffer:vertexBuffer offset:vertexOffset atIndex:0];
		[_encoder setVertexBytes:&desc.uniforms length:sizeof(desc.uniforms) atIndex:1];
		[_encoder setFragmentBytes:&desc.uniforms length:sizeof(desc.uniforms) atIndex:1];

		MTLPrimitiveType type = MTLPrimitiveTypeTriangle;
		switch (desc.primitiveType)
		{
			case 1:
				type = MTLPrimitiveTypePoint;
				break;
			case 2:
				type = MTLPrimitiveTypeLine;
				break;
			case 3:
				type = MTLPrimitiveTypeLineStrip;
				break;
			case 5:
				type = MTLPrimitiveTypeTriangleStrip;
				break;
			default:
				type = MTLPrimitiveTypeTriangle;
				break;
		}

		if (desc.indices != nullptr && desc.indexCount > 0)
		{
			NSUInteger indexOffset    = 0;
			id<MTLBuffer> indexBuffer = Allocate(
				desc.indices, desc.indexCount * sizeof(uint16_t), indexOffset);
			[_encoder drawIndexedPrimitives:type
								 indexCount:desc.indexCount
								  indexType:MTLIndexTypeUInt16
								indexBuffer:indexBuffer
						  indexBufferOffset:indexOffset];
		}
		else if (desc.vertexCount > 0)
		{
			[_encoder drawPrimitives:type vertexStart:0 vertexCount:desc.vertexCount];
		}
	}

	void Present(std::vector<uint8_t>* pSnapshot) override
	{
		if (!BeginFrame())
			return;

		[_encoder endEncoding];
		_encoder              = nil;

		id<MTLBuffer> staging = nil;
		if (pSnapshot != nullptr)
		{
			staging = [_device newBufferWithLength:(NSUInteger) _width * _height * 4
										   options:MTLResourceStorageModeShared];
			id<MTLBlitCommandEncoder> blit = [_commandBuffer blitCommandEncoder];
			[blit copyFromTexture:_drawable.texture
							 sourceSlice:0
							 sourceLevel:0
							sourceOrigin:MTLOriginMake(0, 0, 0)
							  sourceSize:MTLSizeMake(_width, _height, 1)
								toBuffer:staging
					   destinationOffset:0
				  destinationBytesPerRow:(NSUInteger) _width * 4
				destinationBytesPerImage:(NSUInteger) _width * _height * 4];
			[blit endEncoding];
		}

		uint64_t serial                  = _frameSerial;
		dispatch_semaphore_t semaphore   = _frameSemaphore;
		std::atomic<uint64_t>* completed = &_completedSerial;
		[_commandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) {
		  uint64_t previous = completed->load();
		  while (previous < serial && !completed->compare_exchange_weak(previous, serial))
		  {
		  }
		  dispatch_semaphore_signal(semaphore);
		}];

		[_commandBuffer presentDrawable:_drawable];
		[_commandBuffer commit];

		if (staging != nil)
		{
			[_commandBuffer waitUntilCompleted];
			pSnapshot->assign(static_cast<const uint8_t*>(staging.contents),
				static_cast<const uint8_t*>(staging.contents) + staging.length);
		}

		_commandBuffer = nil;
		_drawable      = nil;
		_frameSerial++;
	}

private:
	struct Ring
	{
		id<MTLBuffer> buffer;
		size_t used = 0;
	};

	id<MTLTexture> NewTexture(const TextureObject& object)
	{
		MTLTextureDescriptor* d = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:PixelFormat(object.format)
										 width:object.width
										height:object.height
									 mipmapped:object.levels > 1];
		d.mipmapLevelCount      = object.levels;
		d.storageMode           = MTLStorageModeShared;
		d.usage                 = MTLTextureUsageShaderRead;
		return [_device newTextureWithDescriptor:d];
	}

	void Rename(TextureObject& object)
	{
		id<MTLTexture> fresh = NewTexture(object);
		std::vector<uint8_t> level;
		for (uint32_t l = 0; l < object.levels; l++)
		{
			uint32_t w = std::max(1u, object.width >> l), h = std::max(1u, object.height >> l);
			size_t rowBytes = RowBytes(object.format, w);
			level.resize(rowBytes * LevelRows(object.format, h));
			MTLRegion region = MTLRegionMake2D(0, 0, w, h);
			[object.texture getBytes:level.data()
						 bytesPerRow:rowBytes
						  fromRegion:region
						 mipmapLevel:l];
			[fresh replaceRegion:region mipmapLevel:l withBytes:level.data() bytesPerRow:rowBytes];
		}
		object.texture       = fresh;
		object.lastUseSerial = 0;
	}

	bool BeginFrame()
	{
		if (_encoder != nil)
			return true;

		if (_commandBuffer == nil)
		{
			dispatch_semaphore_wait(_frameSemaphore, DISPATCH_TIME_FOREVER);
			_drawable = [_layer nextDrawable];
			if (_drawable == nil)
			{
				dispatch_semaphore_signal(_frameSemaphore);
				return false;
			}

			_commandBuffer                             = [_queue commandBuffer];
			_rings[_frameSerial % FramesInFlight].used = 0;
		}

		MTLRenderPassDescriptor* pass        = [MTLRenderPassDescriptor renderPassDescriptor];
		pass.colorAttachments[0].texture     = _drawable.texture;
		pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
		pass.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		pass.depthAttachment.texture         = _depthTexture;
		pass.depthAttachment.loadAction      = MTLLoadActionClear;
		pass.depthAttachment.clearDepth      = 1.0;
		pass.depthAttachment.storeAction     = MTLStoreActionDontCare;

		_encoder           = [_commandBuffer renderCommandEncoderWithDescriptor:pass];
		_currentPipeline   = nil;
		_currentDepthState = nil;
		_lastCull          = -1;
		_lastFill          = -1;
		return _encoder != nil;
	}

	id<MTLBuffer> Allocate(const void* data, size_t bytes, NSUInteger& offset)
	{
		Ring& ring     = _rings[_frameSerial % FramesInFlight];
		size_t aligned = (ring.used + 255) & ~static_cast<size_t>(255);
		if (aligned + bytes > ring.buffer.length)
		{
			// Grow; draws already encoded keep the old buffer alive.
			size_t capacity = std::max<size_t>(ring.buffer.length * 2, bytes + 256);
			ring.buffer     = [_device newBufferWithLength:capacity
                                               options:MTLResourceStorageModeShared];
			aligned         = 0;
		}

		std::memcpy(static_cast<uint8_t*>(ring.buffer.contents) + aligned, data, bytes);
		ring.used = aligned + bytes;
		offset    = aligned;
		return ring.buffer;
	}

	id<MTLRenderPipelineState> DrawPipeline(bool blend, uint32_t src, uint32_t dst, uint32_t op)
	{
		auto key = std::make_tuple(blend, blend ? src : 0, blend ? dst : 0, blend ? op : 0);
		auto it  = _drawPipelines.find(key);
		if (it != _drawPipelines.end())
			return it->second;

		MTLRenderPipelineDescriptor* d    = [MTLRenderPipelineDescriptor new];
		d.vertexFunction                  = _vs;
		d.fragmentFunction                = _fs;
		d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
		// The back buffer is X8R8G8B8: its alpha isn't written and reads as one.
		d.colorAttachments[0].writeMask   = MTLColorWriteMaskRed | MTLColorWriteMaskGreen
										  | MTLColorWriteMaskBlue;
		d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
		if (blend)
		{
			// D3D9 without SEPARATEALPHABLENDENABLE uses the same factors for alpha. BOTHSRCALPHA /
			// BOTHINVSRCALPHA set both factors from the source one.
			MTLBlendFactor srcFactor = BlendFactor(src), dstFactor = BlendFactor(dst);
			if (src == 12)
				srcFactor = MTLBlendFactorSourceAlpha,
				dstFactor = MTLBlendFactorOneMinusSourceAlpha;
			else if (src == 13)
				srcFactor = MTLBlendFactorOneMinusSourceAlpha,
				dstFactor = MTLBlendFactorSourceAlpha;

			d.colorAttachments[0].blendingEnabled             = YES;
			d.colorAttachments[0].sourceRGBBlendFactor        = srcFactor;
			d.colorAttachments[0].destinationRGBBlendFactor   = dstFactor;
			d.colorAttachments[0].sourceAlphaBlendFactor      = srcFactor;
			d.colorAttachments[0].destinationAlphaBlendFactor = dstFactor;
			d.colorAttachments[0].rgbBlendOperation           = BlendOperation(op);
			d.colorAttachments[0].alphaBlendOperation         = BlendOperation(op);
		}

		NSError* error                      = nil;
		id<MTLRenderPipelineState> pipeline = [_device newRenderPipelineStateWithDescriptor:d
																					  error:&error];
		if (pipeline == nil)
			NSLog(@"Metal: pipeline creation failed: %@", error.localizedDescription);
		_drawPipelines[key] = pipeline;
		return pipeline;
	}

	id<MTLRenderPipelineState> ClearPipeline(bool color)
	{
		__strong id<MTLRenderPipelineState>& pipeline = color ? _clearColorPipeline
															  : _clearDepthPipeline;
		if (pipeline != nil)
			return pipeline;

		MTLRenderPipelineDescriptor* d    = [MTLRenderPipelineDescriptor new];
		d.vertexFunction                  = _clearVs;
		d.fragmentFunction                = _clearFs;
		d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
		d.colorAttachments[0].writeMask   = color ? (MTLColorWriteMaskRed | MTLColorWriteMaskGreen
                                                      | MTLColorWriteMaskBlue)
												  : MTLColorWriteMaskNone;
		d.depthAttachmentPixelFormat      = MTLPixelFormatDepth32Float;
		NSError* error                    = nil;
		pipeline = [_device newRenderPipelineStateWithDescriptor:d error:&error];
		if (pipeline == nil)
			NSLog(@"Metal: clear pipeline creation failed: %@", error.localizedDescription);
		return pipeline;
	}

	id<MTLDepthStencilState> DepthState(bool test, bool write, uint32_t func)
	{
		// D3D9 doesn't write depth when the depth test is off.
		auto key = std::make_tuple(test, test && write, test ? func : 8u);
		auto it  = _depthStates.find(key);
		if (it != _depthStates.end())
			return it->second;

		MTLDepthStencilDescriptor* d   = [MTLDepthStencilDescriptor new];
		d.depthCompareFunction         = test ? CompareFunction(func) : MTLCompareFunctionAlways;
		d.depthWriteEnabled            = test && write;
		id<MTLDepthStencilState> state = [_device newDepthStencilStateWithDescriptor:d];
		_depthStates[key]              = state;
		return state;
	}

	id<MTLSamplerState> SamplerState(const SamplerDesc& s)
	{
		auto key = std::make_tuple(s.minFilter, s.magFilter, s.mipFilter, s.addressU, s.addressV);
		auto it  = _samplers.find(key);
		if (it != _samplers.end())
			return it->second;

		MTLSamplerDescriptor* d     = [MTLSamplerDescriptor new];
		d.minFilter                 = MinMagFilter(s.minFilter);
		d.magFilter                 = MinMagFilter(s.magFilter);
		d.mipFilter                 = s.mipFilter == 0 ? MTLSamplerMipFilterNotMipmapped
													   : (s.mipFilter == 1 ? MTLSamplerMipFilterNearest
																		   : MTLSamplerMipFilterLinear);
		d.sAddressMode              = AddressMode(s.addressU);
		d.tAddressMode              = AddressMode(s.addressV);
		id<MTLSamplerState> sampler = [_device newSamplerStateWithDescriptor:d];
		_samplers[key]              = sampler;
		return sampler;
	}

	void SetPipeline(id<MTLRenderPipelineState> pipeline)
	{
		if (pipeline != nil && pipeline != _currentPipeline)
		{
			[_encoder setRenderPipelineState:pipeline];
			_currentPipeline = pipeline;
		}
	}

	void SetDepthState(id<MTLDepthStencilState> state)
	{
		if (state != _currentDepthState)
		{
			[_encoder setDepthStencilState:state];
			_currentDepthState = state;
		}
	}

	CAMetalLayer* _layer = nil;
	id<MTLDevice> _device;
	id<MTLCommandQueue> _queue;
	id<MTLLibrary> _library;
	id<MTLFunction> _vs, _fs, _clearVs, _clearFs;
	id<MTLTexture> _whiteTexture;
	id<MTLTexture> _depthTexture;
	dispatch_semaphore_t _frameSemaphore;

	id<CAMetalDrawable> _drawable;
	id<MTLCommandBuffer> _commandBuffer;
	id<MTLRenderCommandEncoder> _encoder;
	id<MTLRenderPipelineState> _currentPipeline;
	id<MTLDepthStencilState> _currentDepthState;
	int _lastCull = -1, _lastFill = -1;

	Ring _rings[FramesInFlight];
	uint64_t _frameSerial = 1;
	std::atomic<uint64_t> _completedSerial { 0 };

	std::map<std::tuple<bool, uint32_t, uint32_t, uint32_t>, id<MTLRenderPipelineState>>
		_drawPipelines;
	id<MTLRenderPipelineState> _clearColorPipeline, _clearDepthPipeline;
	std::map<std::tuple<bool, bool, uint32_t>, id<MTLDepthStencilState>> _depthStates;
	std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>, id<MTLSamplerState>>
		_samplers;

	int _width  = 0;
	int _height = 0;
};

} // namespace

namespace Platform::macOS::Cocoa
{

MetalRenderer* MetalRenderer::Create(void* metalLayer, int iWidth, int iHeight, bool bVSync)
{
	auto* layer = (__bridge CAMetalLayer*) metalLayer;
	if (layer == nil)
		return nullptr;

	auto* renderer = new MetalRendererImpl();
	if (!renderer->Init(layer, iWidth, iHeight, bVSync))
	{
		delete renderer;
		return nullptr;
	}
	return renderer;
}

bool WritePng(const std::string& szPath, int iWidth, int iHeight, const std::vector<uint8_t>& bgra)
{
	if (iWidth <= 0 || iHeight <= 0 || bgra.size() < static_cast<size_t>(iWidth) * iHeight * 4)
		return false;

	@autoreleasepool
	{
		CGColorSpaceRef rgb        = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGDataProviderRef provider = CGDataProviderCreateWithData(
			nullptr, bgra.data(), static_cast<size_t>(iWidth) * iHeight * 4, nullptr);
		CGImageRef image = CGImageCreate(iWidth, iHeight, 8, 32, static_cast<size_t>(iWidth) * 4,
			rgb, static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little,
			provider, nullptr, false, kCGRenderingIntentDefault);

		NSURL* url       = [NSURL fileURLWithPath:@(szPath.c_str())];
		CGImageDestinationRef destination = CGImageDestinationCreateWithURL(
			(__bridge CFURLRef) url, (__bridge CFStringRef) UTTypePNG.identifier, 1, nullptr);
		bool ok = false;
		if (destination != nullptr)
		{
			CGImageDestinationAddImage(destination, image, nullptr);
			ok = CGImageDestinationFinalize(destination);
			CFRelease(destination);
		}

		CGImageRelease(image);
		CGDataProviderRelease(provider);
		CGColorSpaceRelease(rgb);
		return ok;
	}
}

} // namespace Platform::macOS::Cocoa

// Metal backend of the renderer spike: emulates exactly the fixed-function state that
// ffscene.h uses (and the census found), headless, and writes the same PNG set as
// d3d9_main.cpp so the two can be FLIP-compared.
//
//   ffspike_metal --out scene.png [--unbound r,g,b,a]
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <tuple>

#include "ffscene.h"
#include "png_write.h"

namespace
{

// --- GPU-side mirror of the fixed-function state ---------------------------------

struct GpuLight
{
	float diffuse[4], specular[4], ambient[4], position[4], direction[4];
	float params[4];   // range, att0, att1, att2
	uint32_t info[4];  // type, enabled
};

struct GpuStage
{
	uint32_t a[4]; // colorOp, colorArg1, colorArg2, alphaOp
	uint32_t b[4]; // alphaArg1, alphaArg2, hasTexture, texCoordIndex
	uint32_t c[4]; // useMips, pointSampled
};

struct Uniforms
{
	float world[16], view[16], proj[16], normalMat[16];
	float viewport[4];  // W, H, 1/W, 1/H
	uint32_t v0[4];     // fvf, strideWords, offNormal, offDiffuse
	uint32_t v1[4];     // offTex0, offTex1, lighting, specular
	uint32_t v2[4];     // fog, alphaTest, alphaRef, xyzrhw
	float ambient[4], matDiffuse[4], matAmbient[4], matSpecular[4], matEmissive[4];
	float p0[4];        // power, fogDensity, lodBias
	float fogColor[4], tfactor[4], unboundTex[4];
	GpuLight lights[2];
	GpuStage stages[3];
	uint32_t inject[4]; // deliberate defects for threshold calibration: modulate2x, halfpixel, normalize
};

const char* kShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Light { float4 diffuse, specular, ambient, position, direction; float4 params; uint4 info; };
struct Stage { uint4 a; uint4 b; uint4 c; };
struct U {
	float4x4 world, view, proj, normalMat;
	float4 viewport;
	uint4 v0, v1, v2;
	float4 ambient, matDiffuse, matAmbient, matSpecular, matEmissive;
	float4 p0;
	float4 fogColor, tfactor, unboundTex;
	Light lights[2];
	Stage stages[3];
	uint4 inject;
};

struct VOut {
	float4 pos [[position]];
	float psize [[point_size]];
	float4 diffuse;
	float4 specular;
	float2 uv0;
	float2 uv1;
	float eyeDepth;
};

constant uint FVF_XYZRHW = 0x004, FVF_NORMAL = 0x010, FVF_DIFFUSE = 0x040, FVF_TEX1 = 0x100, FVF_TEX2 = 0x200;

static float4 unpackColor(uint c)
{
	return float4((c >> 16) & 255u, (c >> 8) & 255u, c & 255u, c >> 24) / 255.0;
}

static float f(device const uint* vb, uint i) { return as_type<float>(vb[i]); }

vertex VOut vs(uint vid [[vertex_id]], device const uint* vb [[buffer(0)]], constant U& u [[buffer(1)]])
{
	VOut o;
	uint base = vid * u.v0.y;
	uint fvf = u.v0.x;
	bool hasDiffuse = (fvf & FVF_DIFFUSE) != 0;
	float4 vdiff = hasDiffuse ? unpackColor(vb[base + u.v0.w]) : float4(1.0);
	o.uv0 = (fvf & (FVF_TEX1 | FVF_TEX2)) ? float2(f(vb, base + u.v1.x), f(vb, base + u.v1.x + 1)) : float2(0.0);
	o.uv1 = (fvf & FVF_TEX2) ? float2(f(vb, base + u.v1.y), f(vb, base + u.v1.y + 1)) : o.uv0;
	o.psize = 1.0;
	o.specular = float4(0.0);

	if (fvf & FVF_XYZRHW)
	{
		// D3D9 pixel centres sit on integer coordinates, Metal's on +0.5.
		float x = f(vb, base) + 0.5, y = f(vb, base + 1), z = f(vb, base + 2), rhw = f(vb, base + 3);
		y += 0.5;
		float w = 1.0 / rhw;
		float nx = x * u.viewport.z * 2.0 - 1.0, ny = 1.0 - y * u.viewport.w * 2.0;
		o.pos = float4(nx * w, ny * w, z * w, w);
		o.eyeDepth = w;
		o.diffuse = vdiff;
		return o;
	}

	float4 p = float4(f(vb, base), f(vb, base + 1), f(vb, base + 2), 1.0);
	float4 vp = u.view * (u.world * p);
	float4 cp = u.proj * vp;
	// Same half-pixel shift for transformed geometry, applied in clip space.
	if (u.inject.y == 0)
	{
		cp.x += cp.w * u.viewport.z;
		cp.y -= cp.w * u.viewport.w;
	}
	o.pos = cp;
	o.eyeDepth = cp.w;

	if (u.v1.z == 0) // lighting off
	{
		o.diffuse = vdiff;
		return o;
	}

	float3 n = (fvf & FVF_NORMAL) ? float3(f(vb, base + u.v0.z), f(vb, base + u.v0.z + 1), f(vb, base + u.v0.z + 2)) : float3(0.0);
	float3 N = (u.normalMat * float4(n, 0.0)).xyz; // D3DRS_NORMALIZENORMALS is FALSE in the client
	if (u.inject.z != 0)
		N = normalize(N);
	float3 V = vp.xyz;
	float4 matD = hasDiffuse ? vdiff : u.matDiffuse; // COLORVERTEX TRUE, DIFFUSEMATERIALSOURCE COLOR1
	float3 amb = u.ambient.rgb, dif = float3(0.0), spe = float3(0.0);
	for (int i = 0; i < 2; i++)
	{
		Light L = u.lights[i];
		if (L.info.y == 0)
			continue;
		float3 ldir;
		float atten = 1.0;
		if (L.info.x == 3) // directional
			ldir = normalize(-L.direction.xyz);
		else
		{
			float3 d = L.position.xyz - V;
			float dist = length(d);
			if (dist > L.params.x)
				continue;
			ldir = d / dist;
			atten = 1.0 / (L.params.y + L.params.z * dist + L.params.w * dist * dist);
		}
		amb += L.ambient.rgb * atten;
		float ndl = dot(N, ldir);
		if (ndl > 0.0)
		{
			dif += L.diffuse.rgb * ndl * atten;
			if (u.v1.w != 0) // specular, D3DRS_LOCALVIEWER TRUE
			{
				float3 h = normalize(normalize(-V) + ldir);
				float ndh = dot(N, h);
				if (ndh > 0.0)
					spe += L.specular.rgb * pow(ndh, u.p0.x) * atten;
			}
		}
	}
	o.diffuse = float4(saturate(u.matEmissive.rgb + u.matAmbient.rgb * amb + matD.rgb * dif), saturate(matD.a));
	o.specular = float4(saturate(u.matSpecular.rgb * spe), 0.0);
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
		default: v = float4(1.0); break;
	}
	if (arg & 0x20u) v = v.aaaa;
	if (arg & 0x10u) v = 1.0 - v;
	return v;
}

static float4 combine(uint op, float4 a1, float4 a2)
{
	switch (op)
	{
		case 2: return a1;
		case 3: return a2;
		case 4: return a1 * a2;
		case 5: return saturate(a1 * a2 * 2.0);
		case 6: return saturate(a1 * a2 * 4.0);
		case 7: return saturate(a1 + a2);
		default: return a1;
	}
}

static float4 sampleStage(constant U& u, uint i, VOut in, texture2d<float> t, sampler s)
{
	Stage st = u.stages[i];
	if (st.b.z == 0)
		return u.unboundTex;
	float2 uv = st.b.w == 0 ? in.uv0 : in.uv1;
	if (st.c.y != 0)
	{
		// WARP picks texel floor(uv * size) even when the interpolated coordinate lands
		// exactly on a texel edge; Apple GPUs can land a hair below it. Snap to the
		// centre of the texel WARP would pick.
		float2 size = float2(t.get_width(), t.get_height());
		uv = (floor(uv * size + 1.0 / 256.0) + 0.5) / size;
	}
	return st.c.x != 0 ? t.sample(s, uv, bias(u.p0.z)) : t.sample(s, uv);
}

fragment float4 fs(VOut in [[stage_in]], constant U& u [[buffer(1)]],
	texture2d<float> t0 [[texture(0)]], texture2d<float> t1 [[texture(1)]], texture2d<float> t2 [[texture(2)]],
	sampler s0 [[sampler(0)]], sampler s1 [[sampler(1)]], sampler s2 [[sampler(2)]])
{
	float4 tex[3] = { sampleStage(u, 0, in, t0, s0), sampleStage(u, 1, in, t1, s1), sampleStage(u, 2, in, t2, s2) };
	float4 current = in.diffuse;
	for (uint i = 0; i < 3; i++)
	{
		Stage st = u.stages[i];
		if (st.a.x == 1) // D3DTOP_DISABLE ends the cascade
			break;
		float4 c1 = argValue(st.a.y, in.diffuse, current, tex[i], u.tfactor, in.specular);
		float4 c2 = argValue(st.a.z, in.diffuse, current, tex[i], u.tfactor, in.specular);
		float3 rgb = combine(u.inject.x != 0 && st.a.x == 5 ? 4u : st.a.x, c1, c2).rgb;
		float a = current.a;
		if (st.a.w != 1)
		{
			float4 a1 = argValue(st.b.x, in.diffuse, current, tex[i], u.tfactor, in.specular);
			float4 a2 = argValue(st.b.y, in.diffuse, current, tex[i], u.tfactor, in.specular);
			a = combine(st.a.w, a1, a2).a;
		}
		current = float4(rgb, a);
	}
	if (u.v1.w != 0)
		current.rgb = saturate(current.rgb + in.specular.rgb);
	if (u.v2.y != 0)
	{
		// D3DCMP_GREATER against D3DRS_ALPHAREF; WARP compares the unquantised alpha.
		if (!(saturate(current.a) > float(u.v2.z) / 255.0))
			discard_fragment();
	}
	if (u.v2.x != 0) // D3DFOG_EXP2 table fog on eye depth (W-friendly projection)
	{
		float d = in.eyeDepth * u.p0.y;
		float fogf = saturate(exp(-d * d));
		current.rgb = mix(u.fogColor.rgb, current.rgb, fogf);
	}
	return current;
}
)MSL";

void ColorToFloat(uint32_t c, float* out)
{
	out[0] = ((c >> 16) & 255) / 255.0f;
	out[1] = ((c >> 8) & 255) / 255.0f;
	out[2] = (c & 255) / 255.0f;
	out[3] = (c >> 24) / 255.0f;
}

ff::Mat4 Inverse(const ff::Mat4& a)
{
	// Gauss-Jordan; the matrices here are well conditioned.
	float m[4][8];
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 8; j++)
			m[i][j] = j < 4 ? a.m[i][j] : (j - 4 == i ? 1.0f : 0.0f);
	for (int c = 0; c < 4; c++)
	{
		int piv = c;
		for (int r = c + 1; r < 4; r++)
			if (std::fabs(m[r][c]) > std::fabs(m[piv][c]))
				piv = r;
		for (int j = 0; j < 8; j++)
			std::swap(m[c][j], m[piv][j]);
		float d = m[c][c];
		for (int j = 0; j < 8; j++)
			m[c][j] /= d;
		for (int r = 0; r < 4; r++)
			if (r != c)
			{
				float k = m[r][c];
				for (int j = 0; j < 8; j++)
					m[r][j] -= k * m[c][j];
			}
	}
	ff::Mat4 r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = m[i][j + 4];
	return r;
}

ff::Mat4 Transpose(const ff::Mat4& a)
{
	ff::Mat4 r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = a.m[j][i];
	return r;
}

ff::V3 TransformPoint(ff::V3 p, const ff::Mat4& m)
{
	return { p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
		p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
		p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2] };
}

ff::V3 TransformDir(ff::V3 p, const ff::Mat4& m)
{
	return { p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0], p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1],
		p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] };
}

MTLSamplerMinMagFilter Filter(uint32_t f)
{
	return f == ff::TEXF_POINT ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

MTLSamplerAddressMode Address(uint32_t a)
{
	switch (a)
	{
		case ff::TADDRESS_MIRROR:
			return MTLSamplerAddressModeMirrorRepeat;
		case ff::TADDRESS_CLAMP:
			return MTLSamplerAddressModeClampToEdge;
		default:
			return MTLSamplerAddressModeRepeat;
	}
}

MTLBlendFactor Blend(uint32_t b)
{
	switch (b)
	{
		case ff::BLEND_ZERO:
			return MTLBlendFactorZero;
		case ff::BLEND_ONE:
			return MTLBlendFactorOne;
		case ff::BLEND_SRCCOLOR:
			return MTLBlendFactorSourceColor;
		case ff::BLEND_INVSRCCOLOR:
			return MTLBlendFactorOneMinusSourceColor;
		case ff::BLEND_SRCALPHA:
			return MTLBlendFactorSourceAlpha;
		default:
			return MTLBlendFactorOneMinusSourceAlpha;
	}
}

// BC1/2/3 decoded on the CPU with the D3D formulas (palette entries rounded to
// nearest), used to test whether GPU block decoding explains a difference.
void DecodeBC(const ff::Texture& t, const std::vector<uint8_t>& level, int w, int h, std::vector<uint8_t>& bgra)
{
	bgra.assign((size_t) w * h * 4, 0);
	int bw = (w + 3) / 4, bh = (h + 3) / 4;
	size_t bs = t.format == ff::FMT_DXT1 ? 8 : 16;
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++)
		{
			const uint8_t* b = &level[((size_t) by * bw + bx) * bs];
			const uint8_t* cb = t.format == ff::FMT_DXT1 ? b : b + 8;
			uint16_t c0 = (uint16_t) (cb[0] | cb[1] << 8), c1 = (uint16_t) (cb[2] | cb[3] << 8);
			float pal[4][4];
			auto expand = [](uint16_t c, float* o) {
				o[0] = ((c >> 11) & 31) * 255.0f / 31.0f;
				o[1] = ((c >> 5) & 63) * 255.0f / 63.0f;
				o[2] = (c & 31) * 255.0f / 31.0f;
				o[3] = 255.0f;
			};
			expand(c0, pal[0]);
			expand(c1, pal[1]);
			bool four = t.format != ff::FMT_DXT1 || c0 > c1;
			for (int k = 0; k < 4; k++)
			{
				if (four)
				{
					pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3.0f;
					pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3.0f;
				}
				else
				{
					pal[2][k] = (pal[0][k] + pal[1][k]) / 2.0f;
					pal[3][k] = 0.0f;
				}
			}
			uint32_t bits = (uint32_t) (cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t) cb[7] << 24);
			float apal[8] = {};
			uint64_t abits = 0;
			if (t.format == ff::FMT_DXT5)
			{
				apal[0] = b[0], apal[1] = b[1];
				for (int i = 1; i < 7; i++)
					apal[i + 1] = b[0] > b[1] ? ((7 - i) * b[0] + i * b[1]) / 7.0f : (i < 5 ? ((5 - i) * b[0] + i * b[1]) / 5.0f : (i == 5 ? 0.0f : 255.0f));
				for (int k = 0; k < 6; k++)
					abits |= (uint64_t) b[2 + k] << (8 * k);
			}
			for (int i = 0; i < 16; i++)
			{
				int x = bx * 4 + i % 4, y = by * 4 + i / 4;
				if (x >= w || y >= h)
					continue;
				const float* c = pal[(bits >> (2 * i)) & 3];
				float a = c[3];
				if (t.format == ff::FMT_DXT3)
					a = ((b[i / 2] >> ((i & 1) * 4)) & 15) * 17.0f;
				else if (t.format == ff::FMT_DXT5)
					a = apal[(abits >> (3 * i)) & 7];
				uint8_t* o = &bgra[((size_t) y * w + x) * 4];
				o[0] = (uint8_t) std::lround(c[2]), o[1] = (uint8_t) std::lround(c[1]), o[2] = (uint8_t) std::lround(c[0]), o[3] = (uint8_t) std::lround(a);
			}
		}
}

struct Renderer
{
	bool cpuBC = false;
	uint32_t inject[4] = {};
	float fogScale = 1.0f;
	id<MTLDevice> device;
	id<MTLCommandQueue> queue;
	id<MTLLibrary> library;
	std::map<std::tuple<bool, uint32_t, uint32_t>, id<MTLRenderPipelineState>> pipelines;
	std::map<std::pair<bool, bool>, id<MTLDepthStencilState>> depthStates;
	std::vector<id<MTLTexture>> textures;
	float unbound[4] = { 1, 1, 1, 1 };

	bool Init()
	{
		device = MTLCreateSystemDefaultDevice();
		if (device == nil)
			return false;
		queue                  = [device newCommandQueue];
		MTLCompileOptions* opt = [MTLCompileOptions new];
		opt.mathMode           = MTLMathModeSafe;
		NSError* err           = nil;
		library = [device newLibraryWithSource:[NSString stringWithUTF8String:kShaderSource] options:opt error:&err];
		if (library == nil)
		{
			std::printf("shader compile failed: %s\n", err.localizedDescription.UTF8String);
			return false;
		}
		std::printf("device: %s\n", device.name.UTF8String);
		return true;
	}

	id<MTLRenderPipelineState> Pipeline(bool blend, uint32_t src, uint32_t dst)
	{
		auto key = std::make_tuple(blend, src, dst);
		auto it  = pipelines.find(key);
		if (it != pipelines.end())
			return it->second;
		MTLRenderPipelineDescriptor* d     = [MTLRenderPipelineDescriptor new];
		d.vertexFunction                   = [library newFunctionWithName:@"vs"];
		d.fragmentFunction                 = [library newFunctionWithName:@"fs"];
		d.colorAttachments[0].pixelFormat  = MTLPixelFormatBGRA8Unorm;
		d.depthAttachmentPixelFormat       = MTLPixelFormatDepth32Float;
		d.colorAttachments[0].blendingEnabled = blend;
		if (blend)
		{
			// D3D9 without SEPARATEALPHABLENDENABLE uses the same factors for alpha.
			d.colorAttachments[0].sourceRGBBlendFactor        = Blend(src);
			d.colorAttachments[0].destinationRGBBlendFactor   = Blend(dst);
			d.colorAttachments[0].sourceAlphaBlendFactor      = Blend(src);
			d.colorAttachments[0].destinationAlphaBlendFactor = Blend(dst);
		}
		NSError* err                 = nil;
		id<MTLRenderPipelineState> p = [device newRenderPipelineStateWithDescriptor:d error:&err];
		if (p == nil)
			std::printf("pipeline failed: %s\n", err.localizedDescription.UTF8String);
		pipelines[key] = p;
		return p;
	}

	id<MTLDepthStencilState> Depth(bool enable, bool write)
	{
		auto key = std::make_pair(enable, write);
		auto it  = depthStates.find(key);
		if (it != depthStates.end())
			return it->second;
		MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
		d.depthCompareFunction       = enable ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
		d.depthWriteEnabled          = enable && write;
		id<MTLDepthStencilState> s   = [device newDepthStencilStateWithDescriptor:d];
		depthStates[key]             = s;
		return s;
	}

	id<MTLTexture> Upload(const ff::Texture& t)
	{
		bool bc = (t.format == ff::FMT_DXT1 || t.format == ff::FMT_DXT3 || t.format == ff::FMT_DXT5) && !cpuBC;
		MTLPixelFormat pf = MTLPixelFormatBGRA8Unorm;
		if (bc && t.format == ff::FMT_DXT1)
			pf = MTLPixelFormatBC1_RGBA;
		else if (bc && t.format == ff::FMT_DXT3)
			pf = MTLPixelFormatBC2_RGBA;
		else if (bc && t.format == ff::FMT_DXT5)
			pf = MTLPixelFormatBC3_RGBA;
		MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf
																					   width:(NSUInteger) t.width
																					  height:(NSUInteger) t.height
																				   mipmapped:t.levels.size() > 1];
		d.mipmapLevelCount = t.levels.size();
		if (t.format == ff::FMT_X8R8G8B8)
			d.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleGreen, MTLTextureSwizzleBlue, MTLTextureSwizzleOne);
		id<MTLTexture> tex = [device newTextureWithDescriptor:d];
		for (size_t l = 0; l < t.levels.size(); l++)
		{
			int w = std::max(1, t.width >> l), h = std::max(1, t.height >> l);
			std::vector<uint8_t> conv;
			const uint8_t* src = t.levels[l].data();
			NSUInteger rowBytes;
			if (bc)
				rowBytes = (NSUInteger) ((w + 3) / 4) * (t.format == ff::FMT_DXT1 ? 8 : 16);
			else if (t.format == ff::FMT_DXT1 || t.format == ff::FMT_DXT3 || t.format == ff::FMT_DXT5)
			{
				DecodeBC(t, t.levels[l], w, h, conv);
				src      = conv.data();
				rowBytes = (NSUInteger) w * 4;
			}
			else if (t.format == ff::FMT_A4R4G4B4)
			{
				// Expanded by bit replication (x * 17), as D3D/WARP do.
				conv.resize((size_t) w * h * 4);
				for (int i = 0; i < w * h; i++)
				{
					uint16_t v  = (uint16_t) (src[i * 2] | (src[i * 2 + 1] << 8));
					conv[i * 4 + 0] = (uint8_t) ((v & 0xF) * 17);
					conv[i * 4 + 1] = (uint8_t) (((v >> 4) & 0xF) * 17);
					conv[i * 4 + 2] = (uint8_t) (((v >> 8) & 0xF) * 17);
					conv[i * 4 + 3] = (uint8_t) (((v >> 12) & 0xF) * 17);
				}
				src      = conv.data();
				rowBytes = (NSUInteger) w * 4;
			}
			else
				rowBytes = (NSUInteger) w * 4;
			[tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger) w, (NSUInteger) h) mipmapLevel:l withBytes:src bytesPerRow:rowBytes];
		}
		return tex;
	}

	id<MTLSamplerState> Sampler(const ff::Stage& st)
	{
		MTLSamplerDescriptor* d = [MTLSamplerDescriptor new];
		d.minFilter             = Filter(st.minFilter);
		d.magFilter             = Filter(st.magFilter);
		d.mipFilter             = st.mipFilter == ff::TEXF_NONE ? MTLSamplerMipFilterNotMipmapped
								  : st.mipFilter == ff::TEXF_POINT ? MTLSamplerMipFilterNearest
																   : MTLSamplerMipFilterLinear;
		d.sAddressMode = Address(st.addressU);
		d.tAddressMode = Address(st.addressV);
		return [device newSamplerStateWithDescriptor:d];
	}

	void Encode(id<MTLRenderCommandEncoder> enc, const ff::Scene& s, const ff::Draw& d)
	{
		Uniforms u {};
		memcpy(u.world, &d.world, 64);
		memcpy(u.view, &s.view, 64);
		memcpy(u.proj, &s.proj, 64);
		ff::Mat4 nm = Transpose(Inverse(ff::Mul(d.world, s.view)));
		memcpy(u.normalMat, &nm, 64);
		u.viewport[0] = (float) s.width, u.viewport[1] = (float) s.height;
		u.viewport[2] = 1.0f / s.width, u.viewport[3] = 1.0f / s.height;

		uint32_t off = (d.fvf & ff::FVF_XYZRHW) ? 4 : 3;
		uint32_t offN = 0, offD = 0, offT0 = 0, offT1 = 0;
		if (d.fvf & ff::FVF_NORMAL)
			offN = off, off += 3;
		if (d.fvf & ff::FVF_DIFFUSE)
			offD = off, off += 1;
		if (d.fvf & (ff::FVF_TEX1 | ff::FVF_TEX2))
			offT0 = off, off += 2;
		if (d.fvf & ff::FVF_TEX2)
			offT1 = off, off += 2;
		u.v0[0] = d.fvf, u.v0[1] = ff::FvfStride(d.fvf) / 4, u.v0[2] = offN, u.v0[3] = offD;
		u.v1[0] = offT0, u.v1[1] = offT1, u.v1[2] = d.lighting, u.v1[3] = d.specular;
		u.v2[0] = d.fog, u.v2[1] = d.alphaTest, u.v2[2] = d.alphaRef, u.v2[3] = (d.fvf & ff::FVF_XYZRHW) != 0;
		ColorToFloat(s.ambient, u.ambient);
		memcpy(u.matDiffuse, &d.material.diffuse, 16);
		memcpy(u.matAmbient, &d.material.ambient, 16);
		memcpy(u.matSpecular, &d.material.specular, 16);
		memcpy(u.matEmissive, &d.material.emissive, 16);
		u.p0[0] = d.material.power, u.p0[1] = s.fogDensity, u.p0[2] = s.mipLodBias;
		ColorToFloat(s.fogColor, u.fogColor);
		ColorToFloat(d.textureFactor, u.tfactor);
		memcpy(u.unboundTex, unbound, 16);
		memcpy(u.inject, inject, 16);
		u.p0[1] *= fogScale;
		for (int i = 0; i < 2; i++)
		{
			const ff::Light& L = s.lights[i];
			GpuLight& g        = u.lights[i];
			memcpy(g.diffuse, &L.diffuse, 16);
			memcpy(g.specular, &L.specular, 16);
			memcpy(g.ambient, &L.ambient, 16);
			ff::V3 p = TransformPoint(L.position, s.view), dir = TransformDir(L.direction, s.view);
			g.position[0] = p.x, g.position[1] = p.y, g.position[2] = p.z;
			g.direction[0] = dir.x, g.direction[1] = dir.y, g.direction[2] = dir.z;
			g.params[0] = L.range, g.params[1] = L.attenuation0, g.params[2] = L.attenuation1, g.params[3] = L.attenuation2;
			g.info[0] = L.type, g.info[1] = (d.lightMask >> i) & 1;
		}
		bool disabled = false;
		for (int i = 0; i < 3; i++)
		{
			const ff::Stage& st = d.stages[i];
			if (st.colorOp == ff::TOP_DISABLE)
				disabled = true;
			GpuStage& g = u.stages[i];
			g.a[0] = disabled ? ff::TOP_DISABLE : st.colorOp, g.a[1] = st.colorArg1, g.a[2] = st.colorArg2;
			g.a[3] = disabled ? ff::TOP_DISABLE : st.alphaOp;
			g.b[0] = st.alphaArg1, g.b[1] = st.alphaArg2, g.b[2] = st.texture >= 0, g.b[3] = st.texCoordIndex;
			g.c[0] = st.mipFilter != ff::TEXF_NONE;
			g.c[1] = st.minFilter == ff::TEXF_POINT && st.magFilter == ff::TEXF_POINT && st.mipFilter == ff::TEXF_NONE;
			if (st.texture >= 0)
			{
				[enc setFragmentTexture:textures[(size_t) st.texture] atIndex:(NSUInteger) i];
				[enc setFragmentSamplerState:Sampler(st) atIndex:(NSUInteger) i];
			}
			else
			{
				[enc setFragmentTexture:textures[0] atIndex:(NSUInteger) i];
				[enc setFragmentSamplerState:Sampler(st) atIndex:(NSUInteger) i];
			}
		}

		[enc setRenderPipelineState:Pipeline(d.alphaBlend, d.srcBlend, d.destBlend)];
		[enc setDepthStencilState:Depth(d.zEnable, d.zWrite)];
		// D3DCULL_CCW culls counter-clockwise triangles (screen space, y down).
		[enc setFrontFacingWinding:MTLWindingClockwise];
		[enc setCullMode:d.cullMode == ff::CULL_NONE ? MTLCullModeNone : (d.cullMode == ff::CULL_CCW ? MTLCullModeBack : MTLCullModeFront)];
		MTLScissorRect sc = { 0, 0, (NSUInteger) s.width, (NSUInteger) s.height };
		if (d.scissor)
			sc = { (NSUInteger) d.scissorRect[0], (NSUInteger) d.scissorRect[1], (NSUInteger) (d.scissorRect[2] - d.scissorRect[0]),
				(NSUInteger) (d.scissorRect[3] - d.scissorRect[1]) };
		[enc setScissorRect:sc];
		// setVertexBytes is limited to 4 KB, so vertex data always goes through a buffer.
		id<MTLBuffer> vb = [device newBufferWithBytes:d.vertices.data() length:d.vertices.size() options:MTLResourceStorageModeShared];
		[enc setVertexBuffer:vb offset:0 atIndex:0];
		[enc setVertexBytes:&u length:sizeof(u) atIndex:1];
		[enc setFragmentBytes:&u length:sizeof(u) atIndex:1];

		// Metal has no triangle fans: expand to lists.
		std::vector<uint16_t> idx = d.indices;
		MTLPrimitiveType pt       = MTLPrimitiveTypeTriangle;
		switch (d.primType)
		{
			case ff::PT_POINTLIST:
				pt = MTLPrimitiveTypePoint;
				break;
			case ff::PT_LINELIST:
				pt = MTLPrimitiveTypeLine;
				break;
			case ff::PT_LINESTRIP:
				pt = MTLPrimitiveTypeLineStrip;
				break;
			case ff::PT_TRIANGLESTRIP:
				pt = MTLPrimitiveTypeTriangleStrip;
				break;
			case ff::PT_TRIANGLEFAN:
				for (uint32_t i = 0; i < d.primCount; i++)
					idx.insert(idx.end(), { 0, (uint16_t) (i + 1), (uint16_t) (i + 2) });
				break;
			default:
				break;
		}
		if (idx.empty())
		{
			NSUInteger count = d.primType == ff::PT_POINTLIST ? d.primCount
							   : d.primType == ff::PT_LINESTRIP ? d.primCount + 1
							   : d.primType == ff::PT_LINELIST ? d.primCount * 2
							   : d.primType == ff::PT_TRIANGLESTRIP ? d.primCount + 2
																	: d.primCount * 3;
			[enc drawPrimitives:pt vertexStart:0 vertexCount:count];
		}
		else
		{
			id<MTLBuffer> ib = [device newBufferWithBytes:idx.data() length:idx.size() * 2 options:MTLResourceStorageModeShared];
			[enc drawIndexedPrimitives:pt indexCount:idx.size() indexType:MTLIndexTypeUInt16 indexBuffer:ib indexBufferOffset:0];
		}
	}

	bool RenderToPng(const ff::Scene& s, int onlyDraw, const std::string& path)
	{
		MTLTextureDescriptor* cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
																						width:(NSUInteger) s.width
																					   height:(NSUInteger) s.height
																					mipmapped:NO];
		cd.usage                 = MTLTextureUsageRenderTarget;
		cd.storageMode           = MTLStorageModeShared;
		id<MTLTexture> color     = [device newTextureWithDescriptor:cd];
		MTLTextureDescriptor* dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
																						width:(NSUInteger) s.width
																					   height:(NSUInteger) s.height
																					mipmapped:NO];
		dd.usage                 = MTLTextureUsageRenderTarget;
		dd.storageMode           = MTLStorageModePrivate;
		id<MTLTexture> depth     = [device newTextureWithDescriptor:dd];

		float clear[4];
		ColorToFloat(s.clearColor, clear);
		MTLRenderPassDescriptor* rp       = [MTLRenderPassDescriptor renderPassDescriptor];
		rp.colorAttachments[0].texture    = color;
		rp.colorAttachments[0].loadAction = MTLLoadActionClear;
		rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		rp.colorAttachments[0].clearColor = MTLClearColorMake(clear[0], clear[1], clear[2], 1.0);
		rp.depthAttachment.texture        = depth;
		rp.depthAttachment.loadAction     = MTLLoadActionClear;
		rp.depthAttachment.storeAction    = MTLStoreActionDontCare;
		rp.depthAttachment.clearDepth     = 1.0;

		id<MTLCommandBuffer> cb         = [queue commandBuffer];
		id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
		[enc setViewport:(MTLViewport) { 0, 0, (double) s.width, (double) s.height, 0, 1 }];
		for (size_t i = 0; i < s.draws.size(); i++)
			if (onlyDraw < 0 || (size_t) onlyDraw == i)
				Encode(enc, s, s.draws[i]);
		[enc endEncoding];
		[cb commit];
		[cb waitUntilCompleted];
		if (cb.error != nil)
		{
			std::printf("command buffer error: %s\n", cb.error.localizedDescription.UTF8String);
			return false;
		}

		std::vector<uint8_t> bgra((size_t) s.width * s.height * 4), rgba(bgra.size());
		[color getBytes:bgra.data() bytesPerRow:(NSUInteger) s.width * 4 fromRegion:MTLRegionMake2D(0, 0, (NSUInteger) s.width, (NSUInteger) s.height) mipmapLevel:0];
		for (size_t i = 0; i < bgra.size(); i += 4)
			rgba[i] = bgra[i + 2], rgba[i + 1] = bgra[i + 1], rgba[i + 2] = bgra[i], rgba[i + 3] = 255;
		return ff::WritePng(path.c_str(), s.width, s.height, rgba.data());
	}
};

} // namespace

int main(int argc, char** argv)
{
	@autoreleasepool
	{
		std::string out = "ffspike-metal.png";
		Renderer r;
		for (int i = 1; i + 1 < argc; i += 2)
		{
			if (std::strcmp(argv[i], "--out") == 0)
				out = argv[i + 1];
			else if (std::strcmp(argv[i], "--inject") == 0)
			{
				std::string what = argv[i + 1];
				if (what == "modulate2x-as-modulate")
					r.inject[0] = 1;
				else if (what == "no-half-pixel")
					r.inject[1] = 1;
				else if (what == "normalize-normals")
					r.inject[2] = 1;
				else if (what == "fog-density-10pct")
					r.fogScale = 1.1f;
			}
			else if (std::strcmp(argv[i], "--cpu-bc") == 0)
				r.cpuBC = std::strcmp(argv[i + 1], "1") == 0;
			else if (std::strcmp(argv[i], "--unbound") == 0)
				std::sscanf(argv[i + 1], "%f,%f,%f,%f", &r.unbound[0], &r.unbound[1], &r.unbound[2], &r.unbound[3]);
		}
		if (!r.Init())
			return 2;
		ff::Scene scene = ff::BuildScene();
		for (const ff::Texture& t : scene.textures)
			r.textures.push_back(r.Upload(t));
		bool ok          = r.RenderToPng(scene, -1, out);
		std::string stem = out.substr(0, out.size() - 4);
		for (size_t i = 0; i < scene.draws.size(); i++)
			ok &= r.RenderToPng(scene, (int) i, stem + "-" + scene.draws[i].name + ".png");
		std::printf("%s\n", ok ? "wrote images" : "FAILED writing images");
		return ok ? 0 : 1;
	}
}

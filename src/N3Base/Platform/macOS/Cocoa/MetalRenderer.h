#ifndef N3BASE_PLATFORM_MACOS_COCOA_METALRENDERER_H
#define N3BASE_PLATFORM_MACOS_COCOA_METALRENDERER_H

#pragma once

// The Metal backend under the macOS Direct3D 9 device (../Direct3D9_macOS.cpp), which tracks the
// D3D9 state and hands each draw over as a DrawDesc: the fixed-function uniform block, the
// pipeline/depth/raster state and the geometry. Neutral types only (see CocoaApp.h); the numeric
// state values are D3D9's (D3DBLEND_*, D3DCMP_*, D3DTEXF_*, ...).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Platform::macOS::Cocoa
{

/// \brief The fixed-function state the shaders read, laid out like the MSL struct in
/// MetalRenderer.mm (16-byte members only, so the C++ and MSL layouts agree).
struct FixedFunctionUniforms
{
	struct Light
	{
		float diffuse[4], specular[4], ambient[4];
		float position[4];  // view space
		float direction[4]; // view space
		float params[4];    // range, attenuation0, attenuation1, attenuation2
		float spot[4];      // falloff, cos(theta / 2), cos(phi / 2)
		uint32_t info[4];   // type (D3DLIGHTTYPE, 0 = off)
	};

	struct Stage
	{
		uint32_t a[4]; // colorOp, colorArg1, colorArg2, alphaOp
		uint32_t b[4]; // alphaArg1, alphaArg2, hasTexture, texCoordIndex
		uint32_t c[4]; // mipmapped, pointSampled, borderMask (bit 0 U, bit 1 V), borderColor
		uint32_t d[4]; // colorArg0, alphaArg0
	};

	float world[16], view[16], proj[16], normalMat[16];
	float viewport[4];    // x, y, width, height
	float viewportInv[4]; // 1 / width, 1 / height
	uint32_t v0[4];       // fvf, stride in words, normal offset, diffuse offset (in words)
	uint32_t v1[4]; // specular offset, texcoord 0 offset, texcoord 1 offset, point size offset
	uint32_t v2[4]; // lighting, specular enable, local viewer, normalize normals
	uint32_t v3[4]; // fog enable, fog mode (D3DFOGMODE), alpha test function (0 = off), alpha ref
	uint32_t v4
		[4]; // diffuse/ambient/specular/emissive source: 0 material, 1 vertex diffuse, 2 vertex specular
	float ambient[4], matDiffuse[4], matAmbient[4], matSpecular[4], matEmissive[4];
	float p0[4];      // specular power, fog density, fog start, fog end
	float p1[4];      // point size, fog uses range (vertex fog with D3DRS_RANGEFOGENABLE)
	float fogColor[4], tfactor[4];
	float lodBias[4]; // per stage
	Light lights[8];
	Stage stages[3];
};

static_assert(sizeof(FixedFunctionUniforms) <= 4096, "setVertexBytes takes at most 4 KB");
static_assert(sizeof(FixedFunctionUniforms) % 16 == 0, "uniform block layout");

enum class TextureFormat : uint8_t
{
	BGRA8, // also what A4R4G4B4 / A1R5G5B5 / R5G6B5 / X8R8G8B8 are converted to
	BC1,
	BC2,
	BC3,
};

struct SamplerDesc
{
	uint32_t minFilter = 1, magFilter = 1, mipFilter = 0; // D3DTEXF_*
	uint32_t addressU = 1, addressV = 1;                  // D3DTADDRESS_*
};

struct DrawDesc
{
	FixedFunctionUniforms uniforms {};

	// Output merger
	bool blend        = false;
	uint32_t srcBlend = 2, destBlend = 1,
			 blendOp   = 1; // D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD
	bool depthTest     = true;
	bool depthWrite    = true;
	uint32_t depthFunc = 4; // D3DCMP_LESSEQUAL

	// Rasterizer
	uint32_t cullMode  = 3;  // D3DCULL_CCW
	bool wireframe     = false;
	bool scissor       = false;
	int scissorRect[4] = {}; // left, top, right, bottom
	float viewport[6]  = {}; // x, y, width, height, min z, max z

	// Textures (MetalRenderer texture handles; nullptr = unbound)
	void* textures[3]  = {};
	SamplerDesc samplers[3];

	// Geometry: vertex words read by the shaders, triangle fans already turned into lists.
	uint32_t primitiveType  = 4; // D3DPT_*
	const void* vertices    = nullptr;
	size_t vertexBytes      = 0;
	uint32_t vertexCount    = 0; // when not indexed
	const uint16_t* indices = nullptr;
	uint32_t indexCount     = 0;
};

struct ClearDesc
{
	bool color = false, depth = false;
	float rgba[4] = {};
	float z       = 1.0f;
	int rect[4]   = {}; // left, top, right, bottom (already clipped to the viewport)
};

class MetalRenderer
{
public:
	/// \brief A renderer drawing into the CAMetalLayer (from CocoaApp's GameLayer()).
	static MetalRenderer* Create(void* metalLayer, int iWidth, int iHeight, bool bVSync);

	virtual ~MetalRenderer()                                  = default;

	virtual void Resize(int iWidth, int iHeight, bool bVSync) = 0;

	virtual void* CreateTexture(
		uint32_t width, uint32_t height, uint32_t levels, TextureFormat format) = 0;

	/// \brief Replaces a whole mip level (rows of `rowBytes`, block rows for BC formats).
	virtual void UploadTexture(
		void* texture, uint32_t level, const void* data, size_t rowBytes) = 0;

	virtual void DestroyTexture(void* texture)                            = 0;

	virtual void Clear(const ClearDesc& desc)                             = 0;

	virtual void Draw(const DrawDesc& desc)                               = 0;

	/// \brief Shows the frame. With `pSnapshot`, also returns its pixels (BGRA, rows top-down).
	virtual void Present(std::vector<uint8_t>* pSnapshot = nullptr)       = 0;

	virtual int Width() const                                             = 0;
	virtual int Height() const                                            = 0;

protected:
	MetalRenderer() = default;
};

/// \brief Writes BGRA pixels (rows top-down) as an opaque PNG file.
bool WritePng(const std::string& szPath, int iWidth, int iHeight, const std::vector<uint8_t>& bgra);

} // namespace Platform::macOS::Cocoa

#endif // N3BASE_PLATFORM_MACOS_COCOA_METALRENDERER_H

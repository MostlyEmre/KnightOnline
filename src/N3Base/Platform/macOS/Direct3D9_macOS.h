#ifndef N3BASE_PLATFORM_MACOS_DIRECT3D9_MACOS_H
#define N3BASE_PLATFORM_MACOS_DIRECT3D9_MACOS_H

#pragma once

// Shared between the macOS Direct3D 9 device (Direct3D9_macOS.cpp) and its D3DX functions
// (D3DX_macOS.cpp); not part of the platform interface.

#include "../RenderApi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Platform::macOS
{

class MacTexture;

/// \brief Saves the next presented frame as a PNG (testing aid; see InputScript.h).
void RequestSnapshot(const std::string& szPath);

/// \brief Bytes per row of a mip level as LockRect reports them (block rows for DXT formats).
size_t LevelPitch(D3DFORMAT format, UINT width);

/// \brief Rows of a mip level (block rows for DXT formats).
UINT LevelRows(D3DFORMAT format, UINT height);

bool IsCompressedFormat(D3DFORMAT format);

/// \brief Whether textures of this format can be created.
bool IsSupportedTextureFormat(D3DFORMAT format);

/// \brief Pixels of an uncompressed format to 32-bit BGRA (byte order B, G, R, A), the way D3D9
/// expands them (bit replication).
void ToBGRA8(D3DFORMAT format, const uint8_t* src, size_t srcPitch, UINT width, UINT height,
	std::vector<uint8_t>& out);

/// \brief 32-bit BGRA pixels to an uncompressed format, rounding to nearest.
void FromBGRA8(
	D3DFORMAT format, const uint8_t* src, UINT width, UINT height, uint8_t* dest, size_t destPitch);

class MacTexture : public IDirect3DTexture9
{
public:
	MacTexture(UINT width, UINT height, UINT levels, D3DFORMAT format);
	~MacTexture() override;

	DWORD GetLevelCount() override;
	HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) override;
	HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) override;
	HRESULT LockRect(
		UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) override;
	HRESULT UnlockRect(UINT Level) override;

	bool IsValid() const
	{
		return _handle != nullptr;
	}

	void* Handle() const
	{
		return _handle;
	}

	D3DFORMAT Format() const
	{
		return _format;
	}

	UINT LevelWidth(UINT level) const;
	UINT LevelHeight(UINT level) const;

	/// \brief The level's pixels in the texture's format (allocated on first use).
	std::vector<uint8_t>& LevelData(UINT level);

	/// \brief Sends a level's pixels to the GPU.
	void Upload(UINT level);

private:
	UINT _width, _height, _levels;
	D3DFORMAT _format;
	void* _handle = nullptr;
	std::vector<std::vector<uint8_t>> _data;
};

class MacSurface : public IDirect3DSurface9
{
public:
	MacSurface(MacTexture* texture, UINT level);
	~MacSurface() override;

	HRESULT GetDesc(D3DSURFACE_DESC* pDesc) override;
	HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) override;
	HRESULT UnlockRect() override;

	MacTexture* Texture() const
	{
		return _texture;
	}

	UINT Level() const
	{
		return _level;
	}

private:
	MacTexture* _texture;
	UINT _level;
};

} // namespace Platform::macOS

#endif // N3BASE_PLATFORM_MACOS_DIRECT3D9_MACOS_H

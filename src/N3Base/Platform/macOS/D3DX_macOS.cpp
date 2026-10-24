#include "Direct3D9_macOS.h"

#include <FileIO/FileReader.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// The two D3DX functions the client calls: loading the few textures that aren't .dxt files (TGA
// and BMP) and filtering one surface into another (CDFont's mip levels).

namespace Platform::macOS
{

namespace
{

struct Image
{
	UINT width  = 0;
	UINT height = 0;
	std::vector<uint8_t> bgra; // rows top-down
	bool hasAlpha                 = false;
	D3DFORMAT fileFormat          = D3DFMT_UNKNOWN;
	D3DXIMAGE_FILEFORMAT fileType = D3DXIFF_BMP;
};

uint16_t U16(const std::vector<uint8_t>& d, size_t at)
{
	return static_cast<uint16_t>(d[at] | (d[at + 1] << 8));
}

uint32_t U32(const std::vector<uint8_t>& d, size_t at)
{
	return static_cast<uint32_t>(
		d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | (static_cast<uint32_t>(d[at + 3]) << 24));
}

bool DecodeBmp(const std::vector<uint8_t>& d, Image& image)
{
	if (d.size() < 54 || d[0] != 'B' || d[1] != 'M')
		return false;

	uint32_t pixelsAt    = U32(d, 10);
	uint32_t headerSize  = U32(d, 14);
	auto width           = static_cast<int32_t>(U32(d, 18));
	auto height          = static_cast<int32_t>(U32(d, 22));
	uint16_t bits        = U16(d, 28);
	uint32_t compression = U32(d, 30);
	uint32_t colorsUsed  = U32(d, 46);
	if (width <= 0 || height == 0 || compression != 0 || (bits != 8 && bits != 24 && bits != 32))
		return false;

	bool topDown  = height < 0;
	UINT h        = static_cast<UINT>(topDown ? -height : height);
	UINT w        = static_cast<UINT>(width);
	size_t stride = ((static_cast<size_t>(w) * bits + 31) / 32) * 4;
	if (pixelsAt + (stride * h) > d.size())
		return false;

	size_t paletteAt     = 14 + static_cast<size_t>(headerSize);
	size_t paletteColors = bits == 8 ? (colorsUsed != 0 ? colorsUsed : 256) : 0;
	size_t pixelBytes    = bits / 8;

	image.width          = w;
	image.height         = h;
	image.hasAlpha       = false; // D3DX treats 32-bit BMPs as X8R8G8B8
	image.fileFormat = bits == 8 ? D3DFMT_UNKNOWN : (bits == 24 ? D3DFMT_R8G8B8 : D3DFMT_X8R8G8B8);
	image.fileType   = D3DXIFF_BMP;
	image.bgra.assign(static_cast<size_t>(w) * h * 4, 0);

	for (UINT y = 0; y < h; y++)
	{
		size_t row = pixelsAt + (stride * (topDown ? y : h - 1 - y));
		size_t out = static_cast<size_t>(y) * w * 4;
		for (UINT x = 0; x < w; x++, out += 4)
		{
			size_t from = bits == 8 ? paletteAt + (static_cast<size_t>(d[row + x]) * 4)
									: row + (x * pixelBytes);
			bool valid  = bits != 8 || (d[row + x] < paletteColors && from + 3 < d.size());
			for (size_t c = 0; c < 3 && valid; c++)
				image.bgra[out + c] = d[from + c];
			image.bgra[out + 3] = 255;
		}
	}

	return true;
}

bool DecodeTga(const std::vector<uint8_t>& d, Image& image)
{
	if (d.size() < 18)
		return false;

	uint8_t idLength = d[0], colorMapType = d[1], type = d[2];
	uint16_t mapFirst = U16(d, 3), mapLength = U16(d, 5);
	uint8_t mapBits = d[7];
	UINT w = U16(d, 12), h = U16(d, 14);
	uint8_t bits = d[16], descriptor = d[17];
	bool rle     = type >= 9;
	uint8_t base = rle ? static_cast<uint8_t>(type - 8) : type;
	if (w == 0 || h == 0 || (base != 1 && base != 2 && base != 3))
		return false;
	if ((base == 2 && bits != 24 && bits != 32) || (base != 2 && bits != 8))
		return false;

	size_t at         = 18 + static_cast<size_t>(idLength);
	size_t entryBytes = (mapBits + 7) / 8;
	size_t paletteAt  = at;
	size_t paletteEnd = at;
	if (colorMapType == 1)
	{
		paletteEnd = at + (mapLength * entryBytes);
		if (paletteEnd > d.size())
			return false;
		at = paletteEnd;
	}

	size_t pixelBytes = bits / 8;
	size_t count      = static_cast<size_t>(w) * h;
	std::vector<uint8_t> raw(count * pixelBytes);
	if (!rle)
	{
		if (at + raw.size() > d.size())
			return false;
		std::copy_n(d.begin() + static_cast<std::ptrdiff_t>(at), raw.size(), raw.begin());
	}
	else
	{
		for (size_t n = 0; n < count;)
		{
			if (at >= d.size())
				return false;
			uint8_t packet = d[at++];
			size_t run     = (packet & 0x7F) + 1u;
			if (n + run > count)
				return false;

			bool repeat  = (packet & 0x80) != 0;
			size_t bytes = repeat ? pixelBytes : run * pixelBytes;
			if (at + bytes > d.size())
				return false;
			for (size_t k = 0; k < run * pixelBytes; k++)
				raw[(n * pixelBytes) + k] = d[at + (repeat ? k % pixelBytes : k)];
			at += bytes;
			n  += run;
		}
	}

	bool topDown     = (descriptor & 0x20) != 0;
	image.width      = w;
	image.height     = h;
	image.hasAlpha   = base == 2 && bits == 32;
	image.fileFormat = base == 2 ? (bits == 32 ? D3DFMT_A8R8G8B8 : D3DFMT_R8G8B8) : D3DFMT_UNKNOWN;
	image.fileType   = D3DXIFF_TGA;
	image.bgra.assign(count * 4, 0);

	for (UINT y = 0; y < h; y++)
	{
		size_t row = static_cast<size_t>(topDown ? y : h - 1 - y) * w * pixelBytes;
		size_t out = static_cast<size_t>(y) * w * 4;
		for (UINT x = 0; x < w; x++, out += 4)
		{
			size_t p = row + (x * pixelBytes);
			if (base == 2)
			{
				image.bgra[out + 0] = raw[p];
				image.bgra[out + 1] = raw[p + 1];
				image.bgra[out + 2] = raw[p + 2];
				image.bgra[out + 3] = bits == 32 ? raw[p + 3] : 255;
			}
			else if (base == 3)
			{
				image.bgra[out + 0] = image.bgra[out + 1] = image.bgra[out + 2] = raw[p];
				image.bgra[out + 3]                                             = 255;
			}
			else
			{
				size_t index        = raw[p] >= mapFirst ? raw[p] - mapFirst : 0;
				size_t entry        = paletteAt + (index * entryBytes);
				image.bgra[out + 3] = 255;
				if (entryBytes >= 3 && entry + entryBytes <= paletteEnd)
				{
					for (size_t c = 0; c < 3; c++)
						image.bgra[out + c] = d[entry + c];
					if (entryBytes == 4)
					{
						image.bgra[out + 3] = d[entry + 3];
						image.hasAlpha      = true;
					}
				}
			}
		}
	}

	return true;
}

/// \brief Box-filters (shrinking) or bilinearly samples (growing) a BGRA image to a new size.
std::vector<uint8_t> Resample(const std::vector<uint8_t>& src, UINT sw, UINT sh, UINT dw, UINT dh)
{
	if (sw == dw && sh == dh)
		return src;

	std::vector<uint8_t> out(static_cast<size_t>(dw) * dh * 4);
	float sx = static_cast<float>(sw) / static_cast<float>(dw),
		  sy = static_cast<float>(sh) / static_cast<float>(dh);
	for (UINT y = 0; y < dh; y++)
	{
		for (UINT x = 0; x < dw; x++)
		{
			float acc[4] = {};
			float weight = 0.0f;
			auto add     = [&](UINT px, UINT py, float w)
			{
				size_t at = ((static_cast<size_t>(py) * sw) + px) * 4;
				for (size_t c = 0; c < 4; c++)
					acc[c] += src[at + c] * w;
				weight += w;
			};

			if (sx >= 1.0f && sy >= 1.0f)
			{
				UINT x0 = static_cast<UINT>(x * sx),
					 x1 = std::max(x0 + 1, std::min(sw, static_cast<UINT>((x + 1) * sx)));
				UINT y0 = static_cast<UINT>(y * sy),
					 y1 = std::max(y0 + 1, std::min(sh, static_cast<UINT>((y + 1) * sy)));
				for (UINT yy = y0; yy < y1; yy++)
					for (UINT xx = x0; xx < x1; xx++)
						add(xx, yy, 1.0f);
			}
			else
			{
				float fx = std::clamp(((x + 0.5f) * sx) - 0.5f, 0.0f, static_cast<float>(sw - 1));
				float fy = std::clamp(((y + 0.5f) * sy) - 0.5f, 0.0f, static_cast<float>(sh - 1));
				UINT x0 = static_cast<UINT>(fx), y0 = static_cast<UINT>(fy);
				UINT x1 = std::min(x0 + 1, sw - 1), y1 = std::min(y0 + 1, sh - 1);
				float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
				add(x0, y0, (1.0f - tx) * (1.0f - ty));
				add(x1, y0, tx * (1.0f - ty));
				add(x0, y1, (1.0f - tx) * ty);
				add(x1, y1, tx * ty);
			}

			size_t at = ((static_cast<size_t>(y) * dw) + x) * 4;
			for (size_t c = 0; c < 4; c++)
				out[at + c] = static_cast<uint8_t>(
					std::clamp((acc[c] / weight) + 0.5f, 0.0f, 255.0f));
		}
	}

	return out;
}

UINT NextPowerOfTwo(UINT v)
{
	UINT p = 1;
	while (p < v)
		p <<= 1;
	return p;
}

void WriteLevel(MacTexture* texture, UINT level, const std::vector<uint8_t>& bgra)
{
	UINT w = texture->LevelWidth(level), h = texture->LevelHeight(level);
	std::vector<uint8_t>& data = texture->LevelData(level);
	FromBGRA8(texture->Format(), bgra.data(), w, h, data.data(), LevelPitch(texture->Format(), w));
	texture->Upload(level);
}

} // namespace

} // namespace Platform::macOS

using namespace Platform::macOS;

HRESULT D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile, UINT Width,
	UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD /*Filter*/,
	DWORD /*MipFilter*/, D3DCOLOR ColorKey, D3DXIMAGE_INFO* pSrcInfo, PALETTEENTRY* /*pPalette*/,
	LPDIRECT3DTEXTURE9* ppTexture)
{
	if (ppTexture != nullptr)
		*ppTexture = nullptr;
	if (pDevice == nullptr || pSrcFile == nullptr || ppTexture == nullptr)
		return D3DERR_INVALIDCALL;

	FileReader file;
	if (!file.OpenExisting(pSrcFile))
		return D3DERR_INVALIDCALL;

	std::vector<uint8_t> bytes(static_cast<size_t>(file.Size()));
	if (!bytes.empty() && !file.Read(bytes.data(), bytes.size()))
		return D3DERR_INVALIDCALL;
	file.Close();

	Image image;
	std::string name = pSrcFile;
	std::transform(name.begin(), name.end(), name.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	bool decoded = name.ends_with(".tga") ? DecodeTga(bytes, image) : DecodeBmp(bytes, image);
	if (!decoded)
		return D3DERR_INVALIDCALL;

	if (pSrcInfo != nullptr)
	{
		*pSrcInfo                 = {};
		pSrcInfo->Width           = image.width;
		pSrcInfo->Height          = image.height;
		pSrcInfo->Depth           = 1;
		pSrcInfo->MipLevels       = 1;
		pSrcInfo->Format          = image.fileFormat;
		pSrcInfo->ResourceType    = D3DRTYPE_TEXTURE;
		pSrcInfo->ImageFileFormat = image.fileType;
	}

	// The colour key becomes transparent black.
	if (ColorKey != 0)
	{
		for (size_t i = 0; i < image.bgra.size(); i += 4)
		{
			uint32_t argb = static_cast<uint32_t>(
				image.bgra[i] | (image.bgra[i + 1] << 8) | (image.bgra[i + 2] << 16)
				| (static_cast<uint32_t>(image.bgra[i + 3]) << 24));
			if (argb == ColorKey)
				std::memset(&image.bgra[i], 0, 4);
		}
		image.hasAlpha = true;
	}

	if (Format == D3DFMT_UNKNOWN || !IsSupportedTextureFormat(Format) || IsCompressedFormat(Format))
		Format = image.hasAlpha ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;

	// D3DX_DEFAULT (and 0) take the file's size rounded up to a power of two.
	UINT w = Width == 0 || Width == D3DX_DEFAULT ? NextPowerOfTwo(image.width) : Width;
	UINT h = Height == 0 || Height == D3DX_DEFAULT ? NextPowerOfTwo(image.height) : Height;

	LPDIRECT3DTEXTURE9 created = nullptr;
	HRESULT hr                 = pDevice->CreateTexture(
        w, h, MipLevels == D3DX_DEFAULT ? 0 : MipLevels, Usage, Format, Pool, &created, nullptr);
	if (FAILED(hr))
		return hr;

	auto* texture              = static_cast<MacTexture*>(created);
	std::vector<uint8_t> level = Resample(image.bgra, image.width, image.height, w, h);
	for (UINT l = 0; l < texture->GetLevelCount(); l++)
	{
		if (l > 0)
			level = Resample(level, texture->LevelWidth(l - 1), texture->LevelHeight(l - 1),
				texture->LevelWidth(l), texture->LevelHeight(l));
		WriteLevel(texture, l, level);
	}

	*ppTexture = texture;
	return D3D_OK;
}

HRESULT D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9 pDestSurface,
	const PALETTEENTRY* /*pDestPalette*/, const RECT* /*pDestRect*/, LPDIRECT3DSURFACE9 pSrcSurface,
	const PALETTEENTRY* /*pSrcPalette*/, const RECT* /*pSrcRect*/, DWORD /*Filter*/,
	D3DCOLOR /*ColorKey*/)
{
	auto* dest = static_cast<MacSurface*>(pDestSurface);
	auto* src  = static_cast<MacSurface*>(pSrcSurface);
	if (dest == nullptr || src == nullptr || IsCompressedFormat(src->Texture()->Format())
		|| IsCompressedFormat(dest->Texture()->Format()))
		return D3DERR_INVALIDCALL;

	MacTexture* srcTexture = src->Texture();
	UINT sw = srcTexture->LevelWidth(src->Level()), sh = srcTexture->LevelHeight(src->Level());
	std::vector<uint8_t> bgra;
	ToBGRA8(srcTexture->Format(), srcTexture->LevelData(src->Level()).data(),
		LevelPitch(srcTexture->Format(), sw), sw, sh, bgra);

	MacTexture* destTexture = dest->Texture();
	UINT dw = destTexture->LevelWidth(dest->Level()), dh = destTexture->LevelHeight(dest->Level());
	WriteLevel(destTexture, dest->Level(), Resample(bgra, sw, sh, dw, dh));
	return D3D_OK;
}

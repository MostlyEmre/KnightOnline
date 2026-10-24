#include "../RenderApi.h"
#include "NotImplemented.h"

// The Direct3D 9 entry points, backed by Metal (Phase 4).

IDirect3D9* Direct3DCreate9(UINT /*SDKVersion*/)
{
	Platform::macOS::NotImplemented("Direct3DCreate9");
	return nullptr;
}

HRESULT D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE9 /*pDevice*/, LPCSTR /*pSrcFile*/,
	UINT /*Width*/, UINT /*Height*/, UINT /*MipLevels*/, DWORD /*Usage*/, D3DFORMAT /*Format*/,
	D3DPOOL /*Pool*/, DWORD /*Filter*/, DWORD /*MipFilter*/, D3DCOLOR /*ColorKey*/,
	D3DXIMAGE_INFO* /*pSrcInfo*/, PALETTEENTRY* /*pPalette*/, LPDIRECT3DTEXTURE9* ppTexture)
{
	Platform::macOS::NotImplemented("D3DXCreateTextureFromFileExA");
	*ppTexture = nullptr;
	return E_FAIL;
}

HRESULT D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9 /*pDestSurface*/,
	const PALETTEENTRY* /*pDestPalette*/, const RECT* /*pDestRect*/,
	LPDIRECT3DSURFACE9 /*pSrcSurface*/, const PALETTEENTRY* /*pSrcPalette*/,
	const RECT* /*pSrcRect*/, DWORD /*Filter*/, D3DCOLOR /*ColorKey*/)
{
	Platform::macOS::NotImplemented("D3DXLoadSurfaceFromSurface");
	return E_FAIL;
}

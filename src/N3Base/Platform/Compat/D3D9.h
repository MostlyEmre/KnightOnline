#ifndef N3BASE_PLATFORM_COMPAT_D3D9_H
#define N3BASE_PLATFORM_COMPAT_D3D9_H

#pragma once

// The Direct3D 9 API surface the client uses (see port/census/client-census.md), for platforms
// without Direct3D. Names, signatures and numeric values follow d3d9.h / d3d9types.h /
// d3d9caps.h / d3dx9tex.h so call sites stay unchanged; the interfaces are abstract and
// implemented by the platform's renderer (N3Base/Platform/<platform>/...).

#if defined(_WIN32)
#error "Compat/D3D9.h is for non-Windows platforms; include <d3dx9.h> on Windows"
#endif

#include "Win32Types.h"

#include <atomic>
#include <cstdint>

// These definitions mirror the SDK headers: the names, values and widths have to match theirs.
// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,performance-enum-size)

// --- basic types / macros -----------------------------------------------------------

using D3DCOLOR = DWORD;

#define D3DCOLOR_ARGB(a, r, g, b)                                                          \
	(static_cast<D3DCOLOR>(                                                                \
		(((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))
#define D3DCOLOR_RGBA(r, g, b, a)        D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b)           D3DCOLOR_ARGB(0xff, r, g, b)

#define D3D_SDK_VERSION                  (32 | 0x80000000)
#define D3DADAPTER_DEFAULT               0

#define MAKE_D3DHRESULT(code)            (static_cast<HRESULT>(0x88760000u | static_cast<uint32_t>(code)))
#define D3D_OK                           S_OK
#define D3DERR_UNSUPPORTEDCOLOROPERATION MAKE_D3DHRESULT(2073)
#define D3DERR_UNSUPPORTEDCOLORARG       MAKE_D3DHRESULT(2074)
#define D3DERR_TOOMANYOPERATIONS         MAKE_D3DHRESULT(2077)
#define D3DERR_OUTOFVIDEOMEMORY          MAKE_D3DHRESULT(380)
#define D3DERR_DEVICELOST                MAKE_D3DHRESULT(2152)
#define D3DERR_DEVICENOTRESET            MAKE_D3DHRESULT(2153)
#define D3DERR_INVALIDCALL               MAKE_D3DHRESULT(2156)

// --- enums ------------------------------------------------------------------------------

enum D3DFORMAT : DWORD
{
	D3DFMT_UNKNOWN  = 0,
	D3DFMT_R8G8B8   = 20,
	D3DFMT_A8R8G8B8 = 21,
	D3DFMT_X8R8G8B8 = 22,
	D3DFMT_R5G6B5   = 23,
	D3DFMT_X1R5G5B5 = 24,
	D3DFMT_A1R5G5B5 = 25,
	D3DFMT_A4R4G4B4 = 26,
	D3DFMT_D32      = 71,
	D3DFMT_D15S1    = 73,
	D3DFMT_D24S8    = 75,
	D3DFMT_D24X8    = 77,
	D3DFMT_D24X4S4  = 79,
	D3DFMT_D16      = 80,
	D3DFMT_INDEX16  = 101,
	D3DFMT_DXT1     = MAKEFOURCC('D', 'X', 'T', '1'),
	D3DFMT_DXT2     = MAKEFOURCC('D', 'X', 'T', '2'),
	D3DFMT_DXT3     = MAKEFOURCC('D', 'X', 'T', '3'),
	D3DFMT_DXT4     = MAKEFOURCC('D', 'X', 'T', '4'),
	D3DFMT_DXT5     = MAKEFOURCC('D', 'X', 'T', '5'),
};

enum D3DDEVTYPE : DWORD
{
	D3DDEVTYPE_HAL = 1,
	D3DDEVTYPE_REF = 2,
};

enum D3DPOOL : DWORD
{
	D3DPOOL_DEFAULT   = 0,
	D3DPOOL_MANAGED   = 1,
	D3DPOOL_SYSTEMMEM = 2,
};

enum D3DRESOURCETYPE : DWORD
{
	D3DRTYPE_SURFACE = 1,
	D3DRTYPE_TEXTURE = 3,
};

enum D3DMULTISAMPLE_TYPE : DWORD
{
	D3DMULTISAMPLE_NONE = 0,
};

enum D3DSWAPEFFECT : DWORD
{
	D3DSWAPEFFECT_DISCARD = 1,
};

enum D3DPRIMITIVETYPE : DWORD
{
	D3DPT_POINTLIST     = 1,
	D3DPT_LINELIST      = 2,
	D3DPT_LINESTRIP     = 3,
	D3DPT_TRIANGLELIST  = 4,
	D3DPT_TRIANGLESTRIP = 5,
	D3DPT_TRIANGLEFAN   = 6,
};

enum D3DTRANSFORMSTATETYPE : DWORD
{
	D3DTS_VIEW       = 2,
	D3DTS_PROJECTION = 3,
	D3DTS_TEXTURE0   = 16,
};
#define D3DTS_WORLDMATRIX(index) static_cast<D3DTRANSFORMSTATETYPE>((index) + 256)
#define D3DTS_WORLD              D3DTS_WORLDMATRIX(0)

enum D3DRENDERSTATETYPE : DWORD
{
	D3DRS_ZENABLE                = 7,
	D3DRS_FILLMODE               = 8,
	D3DRS_SHADEMODE              = 9,
	D3DRS_ZWRITEENABLE           = 14,
	D3DRS_ALPHATESTENABLE        = 15,
	D3DRS_SRCBLEND               = 19,
	D3DRS_DESTBLEND              = 20,
	D3DRS_CULLMODE               = 22,
	D3DRS_ZFUNC                  = 23,
	D3DRS_ALPHAREF               = 24,
	D3DRS_ALPHAFUNC              = 25,
	D3DRS_DITHERENABLE           = 26,
	D3DRS_ALPHABLENDENABLE       = 27,
	D3DRS_FOGENABLE              = 28,
	D3DRS_SPECULARENABLE         = 29,
	D3DRS_FOGCOLOR               = 34,
	D3DRS_FOGTABLEMODE           = 35,
	D3DRS_FOGSTART               = 36,
	D3DRS_FOGEND                 = 37,
	D3DRS_FOGDENSITY             = 38,
	D3DRS_RANGEFOGENABLE         = 48,
	D3DRS_TEXTUREFACTOR          = 60,
	D3DRS_LIGHTING               = 137,
	D3DRS_AMBIENT                = 139,
	D3DRS_FOGVERTEXMODE          = 140,
	D3DRS_COLORVERTEX            = 141,
	D3DRS_LOCALVIEWER            = 142,
	D3DRS_NORMALIZENORMALS       = 143,
	D3DRS_DIFFUSEMATERIALSOURCE  = 145,
	D3DRS_SPECULARMATERIALSOURCE = 146,
	D3DRS_AMBIENTMATERIALSOURCE  = 147,
	D3DRS_EMISSIVEMATERIALSOURCE = 148,
	D3DRS_POINTSIZE              = 154,
	D3DRS_BLENDOP                = 171,
	D3DRS_SCISSORTESTENABLE      = 174,
};

enum D3DTEXTURESTAGESTATETYPE : DWORD
{
	D3DTSS_COLOROP       = 1,
	D3DTSS_COLORARG1     = 2,
	D3DTSS_COLORARG2     = 3,
	D3DTSS_ALPHAOP       = 4,
	D3DTSS_ALPHAARG1     = 5,
	D3DTSS_ALPHAARG2     = 6,
	D3DTSS_TEXCOORDINDEX = 11,
};

enum D3DSAMPLERSTATETYPE : DWORD
{
	D3DSAMP_ADDRESSU      = 1,
	D3DSAMP_ADDRESSV      = 2,
	D3DSAMP_ADDRESSW      = 3,
	D3DSAMP_BORDERCOLOR   = 4,
	D3DSAMP_MAGFILTER     = 5,
	D3DSAMP_MINFILTER     = 6,
	D3DSAMP_MIPFILTER     = 7,
	D3DSAMP_MIPMAPLODBIAS = 8,
};

enum D3DTEXTUREOP : DWORD
{
	D3DTOP_DISABLE    = 1,
	D3DTOP_SELECTARG1 = 2,
	D3DTOP_SELECTARG2 = 3,
	D3DTOP_MODULATE   = 4,
	D3DTOP_MODULATE2X = 5,
	D3DTOP_MODULATE4X = 6,
	D3DTOP_ADD        = 7,
};

#define D3DTA_SELECTMASK     0x0000000f
#define D3DTA_DIFFUSE        0x00000000
#define D3DTA_CURRENT        0x00000001
#define D3DTA_TEXTURE        0x00000002
#define D3DTA_TFACTOR        0x00000003
#define D3DTA_SPECULAR       0x00000004
#define D3DTA_COMPLEMENT     0x00000010
#define D3DTA_ALPHAREPLICATE 0x00000020

enum D3DBLEND : DWORD
{
	D3DBLEND_ZERO         = 1,
	D3DBLEND_ONE          = 2,
	D3DBLEND_SRCCOLOR     = 3,
	D3DBLEND_INVSRCCOLOR  = 4,
	D3DBLEND_SRCALPHA     = 5,
	D3DBLEND_INVSRCALPHA  = 6,
	D3DBLEND_DESTALPHA    = 7,
	D3DBLEND_INVDESTALPHA = 8,
	D3DBLEND_DESTCOLOR    = 9,
	D3DBLEND_INVDESTCOLOR = 10,
};

enum D3DBLENDOP : DWORD
{
	D3DBLENDOP_ADD = 1,
};

enum D3DCMPFUNC : DWORD
{
	D3DCMP_NEVER        = 1,
	D3DCMP_LESS         = 2,
	D3DCMP_EQUAL        = 3,
	D3DCMP_LESSEQUAL    = 4,
	D3DCMP_GREATER      = 5,
	D3DCMP_NOTEQUAL     = 6,
	D3DCMP_GREATEREQUAL = 7,
	D3DCMP_ALWAYS       = 8,
};

enum D3DCULL : DWORD
{
	D3DCULL_NONE = 1,
	D3DCULL_CW   = 2,
	D3DCULL_CCW  = 3,
};

enum D3DZBUFFERTYPE : DWORD
{
	D3DZB_FALSE = 0,
	D3DZB_TRUE  = 1,
};

enum D3DFILLMODE : DWORD
{
	D3DFILL_POINT     = 1,
	D3DFILL_WIREFRAME = 2,
	D3DFILL_SOLID     = 3,
};

enum D3DSHADEMODE : DWORD
{
	D3DSHADE_FLAT    = 1,
	D3DSHADE_GOURAUD = 2,
};

enum D3DFOGMODE : DWORD
{
	D3DFOG_NONE   = 0,
	D3DFOG_EXP    = 1,
	D3DFOG_EXP2   = 2,
	D3DFOG_LINEAR = 3,
};

enum D3DTEXTUREFILTERTYPE : DWORD
{
	D3DTEXF_NONE   = 0,
	D3DTEXF_POINT  = 1,
	D3DTEXF_LINEAR = 2,
};

enum D3DTEXTUREADDRESS : DWORD
{
	D3DTADDRESS_WRAP   = 1,
	D3DTADDRESS_MIRROR = 2,
	D3DTADDRESS_CLAMP  = 3,
	D3DTADDRESS_BORDER = 4,
};

enum D3DMATERIALCOLORSOURCE : DWORD
{
	D3DMCS_MATERIAL = 0,
	D3DMCS_COLOR1   = 1,
	D3DMCS_COLOR2   = 2,
};

enum D3DLIGHTTYPE : DWORD
{
	D3DLIGHT_POINT       = 1,
	D3DLIGHT_SPOT        = 2,
	D3DLIGHT_DIRECTIONAL = 3,
};

#define D3DFVF_XYZ                          0x002
#define D3DFVF_XYZRHW                       0x004
#define D3DFVF_NORMAL                       0x010
#define D3DFVF_PSIZE                        0x020
#define D3DFVF_DIFFUSE                      0x040
#define D3DFVF_SPECULAR                     0x080
#define D3DFVF_TEX1                         0x100
#define D3DFVF_TEX2                         0x200

#define D3DCLEAR_TARGET                     0x00000001L
#define D3DCLEAR_ZBUFFER                    0x00000002L
#define D3DCLEAR_STENCIL                    0x00000004L

#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020L
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040L

#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER  0x00000001
#define D3DPRESENT_RATE_DEFAULT             0x00000000
#define D3DPRESENT_INTERVAL_ONE             0x00000001L
#define D3DPRESENT_INTERVAL_IMMEDIATE       0x80000000L

#define D3DUSAGE_DEPTHSTENCIL               (0x00000002L)

#define D3DLOCK_READONLY                    0x00000010L
#define D3DLOCK_DISCARD                     0x00002000L

#define D3DPTEXTURECAPS_POW2                0x00000002L
#define D3DPTEXTURECAPS_SQUAREONLY          0x00000020L
#define D3DPTEXTURECAPS_MIPMAP              0x00004000L
#define D3DPMISCCAPS_BLENDOP                0x00000800L

#define D3DCS_ALL                           0x00000FFFL

// --- structures -------------------------------------------------------------------------

struct _D3DVECTOR
{
	float x;
	float y;
	float z;
};
using D3DVECTOR = _D3DVECTOR;

struct _D3DCOLORVALUE
{
	float r;
	float g;
	float b;
	float a;
};
using D3DCOLORVALUE = _D3DCOLORVALUE;

struct _D3DMATRIX
{
	union
	{
		struct
		{
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float m[4][4];
	};
};
using D3DMATRIX = _D3DMATRIX;

struct _D3DMATERIAL9
{
	D3DCOLORVALUE Diffuse;
	D3DCOLORVALUE Ambient;
	D3DCOLORVALUE Specular;
	D3DCOLORVALUE Emissive;
	float Power;
};
using D3DMATERIAL9 = _D3DMATERIAL9;

struct _D3DLIGHT9
{
	D3DLIGHTTYPE Type;
	D3DCOLORVALUE Diffuse;
	D3DCOLORVALUE Specular;
	D3DCOLORVALUE Ambient;
	D3DVECTOR Position;
	D3DVECTOR Direction;
	float Range;
	float Falloff;
	float Attenuation0;
	float Attenuation1;
	float Attenuation2;
	float Theta;
	float Phi;
};
using D3DLIGHT9 = _D3DLIGHT9;

struct _D3DVIEWPORT9
{
	DWORD X;
	DWORD Y;
	DWORD Width;
	DWORD Height;
	float MinZ;
	float MaxZ;
};
using D3DVIEWPORT9 = _D3DVIEWPORT9;

struct _D3DRECT
{
	LONG x1;
	LONG y1;
	LONG x2;
	LONG y2;
};
using D3DRECT = _D3DRECT;

struct _D3DLOCKED_RECT
{
	INT Pitch;
	void* pBits;
};
using D3DLOCKED_RECT = _D3DLOCKED_RECT;

struct _D3DSURFACE_DESC
{
	D3DFORMAT Format;
	D3DRESOURCETYPE Type;
	DWORD Usage;
	D3DPOOL Pool;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	DWORD MultiSampleQuality;
	UINT Width;
	UINT Height;
};
using D3DSURFACE_DESC = _D3DSURFACE_DESC;

struct _D3DDISPLAYMODE
{
	UINT Width;
	UINT Height;
	UINT RefreshRate;
	D3DFORMAT Format;
};
using D3DDISPLAYMODE = _D3DDISPLAYMODE;

struct _D3DPRESENT_PARAMETERS_
{
	UINT BackBufferWidth;
	UINT BackBufferHeight;
	D3DFORMAT BackBufferFormat;
	UINT BackBufferCount;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	DWORD MultiSampleQuality;
	D3DSWAPEFFECT SwapEffect;
	HWND hDeviceWindow;
	BOOL Windowed;
	BOOL EnableAutoDepthStencil;
	D3DFORMAT AutoDepthStencilFormat;
	DWORD Flags;
	UINT FullScreen_RefreshRateInHz;
	UINT PresentationInterval;
};
using D3DPRESENT_PARAMETERS = _D3DPRESENT_PARAMETERS_;

struct _D3DCLIPSTATUS9
{
	DWORD ClipUnion;
	DWORD ClipIntersection;
};
using D3DCLIPSTATUS9 = _D3DCLIPSTATUS9;

/// \brief The subset of D3DCAPS9 the client reads; the renderer fills it.
struct _D3DCAPS9
{
	D3DDEVTYPE DeviceType;
	UINT AdapterOrdinal;
	DWORD DevCaps;
	DWORD PrimitiveMiscCaps;
	DWORD RasterCaps;
	DWORD TextureCaps;
	DWORD MaxTextureWidth;
	DWORD MaxTextureHeight;
	DWORD MaxTextureBlendStages;
	DWORD MaxSimultaneousTextures;
	DWORD MaxActiveLights;
	float MaxPointSize;
};
using D3DCAPS9 = _D3DCAPS9;

struct RGNDATA;
struct PALETTEENTRY;

// File formats store these raw (e.g. .n3light), so the layout must match Windows.
static_assert(sizeof(D3DCOLORVALUE) == 16, "D3DCOLORVALUE layout");
static_assert(sizeof(D3DMATERIAL9) == 68, "D3DMATERIAL9 layout");
static_assert(sizeof(D3DLIGHT9) == 104, "D3DLIGHT9 layout");
static_assert(sizeof(D3DMATRIX) == 64, "D3DMATRIX layout");
static_assert(sizeof(D3DVIEWPORT9) == 24, "D3DVIEWPORT9 layout");

// --- interfaces -------------------------------------------------------------------------

/// \brief COM-style reference counting shared by the interfaces below.
class N3D3DUnknown
{
public:
	virtual ~N3D3DUnknown() = default;

	ULONG AddRef()
	{
		return ++_refs;
	}

	ULONG Release()
	{
		ULONG left = --_refs;
		if (left == 0)
			delete this;
		return left;
	}

protected:
	N3D3DUnknown() = default;

private:
	std::atomic<ULONG> _refs { 1 };
};

class IDirect3DResource9 : public N3D3DUnknown
{
};

class IDirect3DSurface9 : public IDirect3DResource9
{
public:
	virtual HRESULT GetDesc(D3DSURFACE_DESC* pDesc)                                       = 0;
	virtual HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
	virtual HRESULT UnlockRect()                                                          = 0;
};

class IDirect3DBaseTexture9 : public IDirect3DResource9
{
public:
	virtual DWORD GetLevelCount() = 0;
};

class IDirect3DTexture9 : public IDirect3DBaseTexture9
{
public:
	virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc)                = 0;
	virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) = 0;
	virtual HRESULT LockRect(
		UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
	virtual HRESULT UnlockRect(UINT Level)                                       = 0;
};

class IDirect3DVertexBuffer9 : public IDirect3DResource9
{
public:
	virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) = 0;
	virtual HRESULT Unlock()                                                              = 0;
};

class IDirect3DIndexBuffer9 : public IDirect3DResource9
{
public:
	virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) = 0;
	virtual HRESULT Unlock()                                                              = 0;
};

class IDirect3DDevice9 : public N3D3DUnknown
{
public:
	virtual HRESULT TestCooperativeLevel()                                                    = 0;
	virtual HRESULT GetDeviceCaps(D3DCAPS9* pCaps)                                            = 0;
	virtual HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)                     = 0;
	virtual HRESULT Present(const RECT* pSourceRect, const RECT* pDestRect,
		HWND hDestWindowOverride, const RGNDATA* pDirtyRegion)                                = 0;
	virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage,
		D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) = 0;
	virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
		IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)                       = 0;
	virtual HRESULT BeginScene()                                                              = 0;
	virtual HRESULT EndScene()                                                                = 0;
	virtual HRESULT Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z,
		DWORD Stencil)                                                                        = 0;
	virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix)       = 0;
	virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix)             = 0;
	virtual HRESULT SetViewport(const D3DVIEWPORT9* pViewport)                                = 0;
	virtual HRESULT GetViewport(D3DVIEWPORT9* pViewport)                                      = 0;
	virtual HRESULT SetMaterial(const D3DMATERIAL9* pMaterial)                                = 0;
	virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9* pLight)                            = 0;
	virtual HRESULT GetLight(DWORD Index, D3DLIGHT9* pLight)                                  = 0;
	virtual HRESULT LightEnable(DWORD Index, BOOL Enable)                                     = 0;
	virtual HRESULT GetLightEnable(DWORD Index, BOOL* pEnable)                                = 0;
	virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)                     = 0;
	virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue)                   = 0;
	virtual HRESULT SetClipStatus(const D3DCLIPSTATUS9* pClipStatus)                          = 0;
	virtual HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture)                  = 0;
	virtual HRESULT GetTextureStageState(
		DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) = 0;
	virtual HRESULT SetTextureStageState(
		DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)                            = 0;
	virtual HRESULT GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) = 0;
	virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)   = 0;
	virtual HRESULT ValidateDevice(DWORD* pNumPasses)                                       = 0;
	virtual HRESULT SetScissorRect(const RECT* pRect)                                       = 0;
	virtual HRESULT DrawPrimitive(
		D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)  = 0;
	virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex,
		UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) = 0;
	virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
		const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)         = 0;
	virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
		UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat,
		const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)         = 0;
	virtual HRESULT SetFVF(DWORD FVF)                                           = 0;
	virtual HRESULT GetFVF(DWORD* pFVF)                                         = 0;
	virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData,
		UINT OffsetInBytes, UINT Stride)                                        = 0;
	virtual HRESULT SetIndices(IDirect3DIndexBuffer9* pIndexData)               = 0;
};

class IDirect3D9 : public N3D3DUnknown
{
public:
	virtual UINT GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) = 0;
	virtual HRESULT EnumAdapterModes(
		UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode)                    = 0;
	virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode)               = 0;
	virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
		DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)                           = 0;
	virtual HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType,
		D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
	virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
		DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
		IDirect3DDevice9** ppReturnedDeviceInterface)                                        = 0;
};

using LPDIRECT3D9             = IDirect3D9*;
using LPDIRECT3DDEVICE9       = IDirect3DDevice9*;
using LPDIRECT3DBASETEXTURE9  = IDirect3DBaseTexture9*;
using LPDIRECT3DTEXTURE9      = IDirect3DTexture9*;
using LPDIRECT3DSURFACE9      = IDirect3DSurface9*;
using LPDIRECT3DVERTEXBUFFER9 = IDirect3DVertexBuffer9*;
using LPDIRECT3DINDEXBUFFER9  = IDirect3DIndexBuffer9*;

/// \brief Implemented by the platform renderer.
IDirect3D9* Direct3DCreate9(UINT SDKVersion);

// --- D3DX (the two functions the client calls) ------------------------------------------

#define D3DX_DEFAULT         (static_cast<UINT>(-1))
#define D3DX_FILTER_TRIANGLE (4 << 0)
#define D3DX_FILTER_MIRROR   (7 << 16)

enum D3DXIMAGE_FILEFORMAT : DWORD
{
	D3DXIFF_BMP = 0,
	D3DXIFF_JPG = 1,
	D3DXIFF_TGA = 2,
	D3DXIFF_PNG = 3,
	D3DXIFF_DDS = 4,
};

struct _D3DXIMAGE_INFO
{
	UINT Width;
	UINT Height;
	UINT Depth;
	UINT MipLevels;
	D3DFORMAT Format;
	D3DRESOURCETYPE ResourceType;
	D3DXIMAGE_FILEFORMAT ImageFileFormat;
};
using D3DXIMAGE_INFO = _D3DXIMAGE_INFO;

/// \brief Implemented by the platform renderer.
HRESULT D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile, UINT Width,
	UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter,
	DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO* pSrcInfo, PALETTEENTRY* pPalette,
	LPDIRECT3DTEXTURE9* ppTexture);
#define D3DXCreateTextureFromFileEx D3DXCreateTextureFromFileExA

/// \brief Implemented by the platform renderer.
HRESULT D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9 pDestSurface,
	const PALETTEENTRY* pDestPalette, const RECT* pDestRect, LPDIRECT3DSURFACE9 pSrcSurface,
	const PALETTEENTRY* pSrcPalette, const RECT* pSrcRect, DWORD Filter, D3DCOLOR ColorKey);

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,performance-enum-size)

#endif // N3BASE_PLATFORM_COMPAT_D3D9_H

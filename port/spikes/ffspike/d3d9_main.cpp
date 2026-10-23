// D3D9 backend of the renderer spike. Renders ffscene.h through the fixed-function
// pipeline and writes PNGs: the whole scene, then each draw on its own.
//
//   ffspike.exe --path native|9on12warp --out scene.png
//
// "native" uses Direct3DCreate9 on the default adapter (on a GPU-less runner that is
// the WARP-backed Basic Render Driver); "9on12warp" forces D3D9On12 on an explicit
// D3D12 WARP device.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "ffscene.h"
#include "png_write.h"

// The scene uses D3D9's numeric values; make sure they really are D3D9's.
#define FF_SAME(a, b) static_assert(static_cast<uint32_t>(a) == static_cast<uint32_t>(b), #a " != " #b)
FF_SAME(ff::TOP_MODULATE2X, D3DTOP_MODULATE2X);
FF_SAME(ff::TOP_ADD, D3DTOP_ADD);
FF_SAME(ff::TOP_SELECTARG1, D3DTOP_SELECTARG1);
FF_SAME(ff::TA_TFACTOR, D3DTA_TFACTOR);
FF_SAME(ff::TA_CURRENT, D3DTA_CURRENT);
FF_SAME(ff::TA_TEXTURE, D3DTA_TEXTURE);
FF_SAME(ff::BLEND_INVSRCALPHA, D3DBLEND_INVSRCALPHA);
FF_SAME(ff::CMP_GREATER, D3DCMP_GREATER);
FF_SAME(ff::CULL_CCW, D3DCULL_CCW);
FF_SAME(ff::TADDRESS_MIRROR, D3DTADDRESS_MIRROR);
FF_SAME(ff::TEXF_LINEAR, D3DTEXF_LINEAR);
FF_SAME(ff::PT_TRIANGLEFAN, D3DPT_TRIANGLEFAN);
FF_SAME(ff::FVF_VNT2, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2);
FF_SAME(ff::FVF_TRANSFORMED, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
FF_SAME(ff::FMT_DXT1, D3DFMT_DXT1);
FF_SAME(ff::FMT_DXT3, D3DFMT_DXT3);
FF_SAME(ff::FMT_DXT5, D3DFMT_DXT5);
FF_SAME(ff::FMT_A4R4G4B4, D3DFMT_A4R4G4B4);
FF_SAME(ff::FMT_X8R8G8B8, D3DFMT_X8R8G8B8);
FF_SAME(ff::LIGHT_DIRECTIONAL, D3DLIGHT_DIRECTIONAL);
static_assert(sizeof(ff::Material) == sizeof(D3DMATERIAL9) && offsetof(ff::Material, power) == offsetof(D3DMATERIAL9, Power));
static_assert(sizeof(ff::Light) == sizeof(D3DLIGHT9) && offsetof(ff::Light, attenuation2) == offsetof(D3DLIGHT9, Attenuation2));

namespace
{

// d3d9on12.h is not in every SDK (MinGW lacks it); same layout as D3D9ON12_ARGS.
struct D3D9On12Args
{
	BOOL Enable9On12;
	IUnknown* pD3D12Device;
	IUnknown* ppD3D12Queues[2];
	UINT NumQueues;
	UINT NodeMask;
};

using PFN_Direct3DCreate9On12 = IDirect3D9*(WINAPI*) (UINT, D3D9On12Args*, UINT);

IDirect3D9* Create9On12Warp()
{
	IDXGIFactory4* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory))))
	{
		std::printf("CreateDXGIFactory1 failed\n");
		return nullptr;
	}
	IDXGIAdapter* warp = nullptr;
	HRESULT hr         = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&warp));
	factory->Release();
	if (FAILED(hr))
	{
		std::printf("EnumWarpAdapter failed: 0x%08lX\n", static_cast<unsigned long>(hr));
		return nullptr;
	}
	ID3D12Device* dev12 = nullptr;
	hr = D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&dev12));
	warp->Release();
	if (FAILED(hr))
	{
		std::printf("D3D12CreateDevice(WARP) failed: 0x%08lX\n", static_cast<unsigned long>(hr));
		return nullptr;
	}
	HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
	auto create  = reinterpret_cast<PFN_Direct3DCreate9On12>(reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9On12")));
	if (create == nullptr)
	{
		std::printf("Direct3DCreate9On12 not exported by d3d9.dll\n");
		dev12->Release();
		return nullptr;
	}
	D3D9On12Args args {};
	args.Enable9On12  = TRUE;
	args.pD3D12Device = dev12;
	IDirect3D9* d3d   = create(D3D_SDK_VERSION, &args, 1);
	dev12->Release();
	return d3d;
}

void PrintAdapter(IDirect3D9* d3d)
{
	D3DADAPTER_IDENTIFIER9 id {};
	d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
	std::printf("adapter: %s | driver: %s | vendor 0x%04lX device 0x%04lX | version %u.%u.%u.%u\n", id.Description, id.Driver,
		static_cast<unsigned long>(id.VendorId), static_cast<unsigned long>(id.DeviceId),
		HIWORD(id.DriverVersion.HighPart), LOWORD(id.DriverVersion.HighPart), HIWORD(id.DriverVersion.LowPart),
		LOWORD(id.DriverVersion.LowPart));

	D3DCAPS9 caps {};
	if (SUCCEEDED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps)))
	{
		std::printf("caps: HWTnL=%d stages=%lu textures=%lu lights=%lu maxPointSize=%.1f fogTable=%d wfog=%d zfog=%d "
					"modulate2x=%d scissor=%d mipLodBias=%d mirror=%d border=%d dxt(?)\n",
			(caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0, static_cast<unsigned long>(caps.MaxTextureBlendStages),
			static_cast<unsigned long>(caps.MaxSimultaneousTextures), static_cast<unsigned long>(caps.MaxActiveLights),
			static_cast<double>(caps.MaxPointSize), (caps.RasterCaps & D3DPRASTERCAPS_FOGTABLE) != 0,
			(caps.RasterCaps & D3DPRASTERCAPS_WFOG) != 0, (caps.RasterCaps & D3DPRASTERCAPS_ZFOG) != 0,
			(caps.TextureOpCaps & D3DTEXOPCAPS_MODULATE2X) != 0, (caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) != 0,
			(caps.RasterCaps & D3DPRASTERCAPS_MIPMAPLODBIAS) != 0, (caps.TextureAddressCaps & D3DPTADDRESSCAPS_MIRROR) != 0,
			(caps.TextureAddressCaps & D3DPTADDRESSCAPS_BORDER) != 0);
	}
	for (D3DFORMAT f : { D3DFMT_DXT1, D3DFMT_DXT3, D3DFMT_DXT5, D3DFMT_A4R4G4B4, D3DFMT_R8G8B8 })
	{
		HRESULT hr = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, f);
		std::printf("texture format 0x%08lX: %s\n", static_cast<unsigned long>(f), SUCCEEDED(hr) ? "supported" : "NOT supported");
	}
}

IDirect3DTexture9* Upload(IDirect3DDevice9* dev, const ff::Texture& t)
{
	IDirect3DTexture9* tex = nullptr;
	HRESULT hr = dev->CreateTexture(static_cast<UINT>(t.width), static_cast<UINT>(t.height), static_cast<UINT>(t.levels.size()), 0,
		static_cast<D3DFORMAT>(t.format), D3DPOOL_MANAGED, &tex, nullptr);
	if (FAILED(hr))
	{
		std::printf("CreateTexture(fmt 0x%08lX) failed: 0x%08lX\n", static_cast<unsigned long>(t.format), static_cast<unsigned long>(hr));
		return nullptr;
	}
	bool bc = t.format == ff::FMT_DXT1 || t.format == ff::FMT_DXT3 || t.format == ff::FMT_DXT5;
	for (size_t l = 0; l < t.levels.size(); l++)
	{
		int w = std::max(1, t.width >> l), h = std::max(1, t.height >> l);
		size_t rowBytes = 0, rows = 0;
		if (bc)
		{
			rowBytes = static_cast<size_t>((w + 3) / 4) * (t.format == ff::FMT_DXT1 ? 8 : 16);
			rows     = static_cast<size_t>((h + 3) / 4);
		}
		else
		{
			rowBytes = static_cast<size_t>(w) * (t.format == ff::FMT_A4R4G4B4 ? 2 : 4);
			rows     = static_cast<size_t>(h);
		}
		D3DLOCKED_RECT lr {};
		tex->LockRect(static_cast<UINT>(l), &lr, nullptr, 0);
		for (size_t r = 0; r < rows; r++)
			std::memcpy(static_cast<uint8_t*>(lr.pBits) + r * static_cast<size_t>(lr.Pitch), t.levels[l].data() + r * rowBytes, rowBytes);
		tex->UnlockRect(static_cast<UINT>(l));
	}
	return tex;
}

void RenderDraw(IDirect3DDevice9* dev, const ff::Scene& s, const ff::Draw& d, IDirect3DTexture9* const* textures)
{
	dev->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&d.world));
	dev->SetRenderState(D3DRS_LIGHTING, d.lighting);
	dev->SetRenderState(D3DRS_SPECULARENABLE, d.specular);
	dev->SetRenderState(D3DRS_ZENABLE, d.zEnable ? D3DZB_TRUE : D3DZB_FALSE);
	dev->SetRenderState(D3DRS_ZWRITEENABLE, d.zWrite);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, d.alphaBlend);
	dev->SetRenderState(D3DRS_SRCBLEND, d.srcBlend);
	dev->SetRenderState(D3DRS_DESTBLEND, d.destBlend);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, d.alphaTest);
	dev->SetRenderState(D3DRS_ALPHAREF, d.alphaRef);
	dev->SetRenderState(D3DRS_FOGENABLE, d.fog);
	dev->SetRenderState(D3DRS_CULLMODE, d.cullMode);
	dev->SetRenderState(D3DRS_TEXTUREFACTOR, d.textureFactor);
	dev->SetRenderState(D3DRS_SCISSORTESTENABLE, d.scissor);
	if (d.scissor)
	{
		RECT rc = { d.scissorRect[0], d.scissorRect[1], d.scissorRect[2], d.scissorRect[3] };
		dev->SetScissorRect(&rc);
	}
	dev->SetMaterial(reinterpret_cast<const D3DMATERIAL9*>(&d.material));
	for (DWORD i = 0; i < 2; i++)
		dev->LightEnable(i, (d.lightMask >> i) & 1);

	bool disabled = false;
	for (DWORD i = 0; i < 4; i++)
	{
		const ff::Stage st = i < 3 ? d.stages[i] : ff::Stage {};
		if (st.colorOp == ff::TOP_DISABLE)
			disabled = true;
		dev->SetTexture(i, st.texture >= 0 ? textures[st.texture] : nullptr);
		dev->SetTextureStageState(i, D3DTSS_COLOROP, disabled ? static_cast<DWORD>(D3DTOP_DISABLE) : st.colorOp);
		dev->SetTextureStageState(i, D3DTSS_COLORARG1, st.colorArg1);
		dev->SetTextureStageState(i, D3DTSS_COLORARG2, st.colorArg2);
		dev->SetTextureStageState(i, D3DTSS_ALPHAOP, disabled ? static_cast<DWORD>(D3DTOP_DISABLE) : st.alphaOp);
		dev->SetTextureStageState(i, D3DTSS_ALPHAARG1, st.alphaArg1);
		dev->SetTextureStageState(i, D3DTSS_ALPHAARG2, st.alphaArg2);
		dev->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, st.texCoordIndex);
		dev->SetSamplerState(i, D3DSAMP_MINFILTER, st.minFilter);
		dev->SetSamplerState(i, D3DSAMP_MAGFILTER, st.magFilter);
		dev->SetSamplerState(i, D3DSAMP_MIPFILTER, st.mipFilter);
		dev->SetSamplerState(i, D3DSAMP_ADDRESSU, st.addressU);
		dev->SetSamplerState(i, D3DSAMP_ADDRESSV, st.addressV);
	}

	dev->SetFVF(d.fvf);
	UINT stride = ff::FvfStride(d.fvf);
	if (!d.indices.empty())
		dev->DrawIndexedPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(d.primType), 0, ff::VertexCount(d), d.primCount, d.indices.data(),
			D3DFMT_INDEX16, d.vertices.data(), stride);
	else
		dev->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(d.primType), d.primCount, d.vertices.data(), stride);
	(void) s;
}

bool RenderToPng(IDirect3DDevice9* dev, const ff::Scene& s, IDirect3DTexture9* const* textures, int onlyDraw, const std::string& path)
{
	dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, s.clearColor, 1.0f, 0);
	dev->BeginScene();

	dev->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(&s.view));
	dev->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(&s.proj));
	// CN3Eng::Init / CN3Camera::Apply globals
	dev->SetRenderState(D3DRS_DITHERENABLE, TRUE);
	dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
	dev->SetRenderState(D3DRS_AMBIENT, s.ambient);
	dev->SetRenderState(D3DRS_FOGCOLOR, s.fogColor);
	dev->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_EXP2);
	dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_EXP2);
	dev->SetRenderState(D3DRS_RANGEFOGENABLE, TRUE);
	dev->SetRenderState(D3DRS_FOGSTART, std::bit_cast<DWORD>(48.0f * 0.75f));
	dev->SetRenderState(D3DRS_FOGEND, std::bit_cast<DWORD>(48.0f));
	dev->SetRenderState(D3DRS_FOGDENSITY, std::bit_cast<DWORD>(s.fogDensity));
	for (DWORD i = 0; i < 8; i++)
		dev->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, std::bit_cast<DWORD>(s.mipLodBias));
	for (DWORD i = 0; i < 2; i++)
		dev->SetLight(i, reinterpret_cast<const D3DLIGHT9*>(&s.lights[i]));

	for (size_t i = 0; i < s.draws.size(); i++)
		if (onlyDraw < 0 || static_cast<size_t>(onlyDraw) == i)
			RenderDraw(dev, s, s.draws[i], textures);

	dev->EndScene();

	IDirect3DSurface9* bb  = nullptr;
	IDirect3DSurface9* sys = nullptr;
	dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
	dev->CreateOffscreenPlainSurface(static_cast<UINT>(s.width), static_cast<UINT>(s.height), D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
	HRESULT hr = dev->GetRenderTargetData(bb, sys);
	if (FAILED(hr))
	{
		std::printf("GetRenderTargetData failed: 0x%08lX\n", static_cast<unsigned long>(hr));
		return false;
	}
	D3DLOCKED_RECT lr {};
	sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
	std::vector<uint8_t> rgba(static_cast<size_t>(s.width) * s.height * 4);
	for (int y = 0; y < s.height; y++)
		for (int x = 0; x < s.width; x++)
		{
			const uint8_t* p = static_cast<const uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch + static_cast<size_t>(x) * 4;
			uint8_t* o       = &rgba[(static_cast<size_t>(y) * s.width + x) * 4];
			o[0] = p[2], o[1] = p[1], o[2] = p[0], o[3] = 255;
		}
	sys->UnlockRect();
	sys->Release();
	bb->Release();
	return ff::WritePng(path.c_str(), s.width, s.height, rgba.data());
}

} // namespace

int main(int argc, char** argv)
{
	std::string path = "native", out = "ffspike.png";
	for (int i = 1; i + 1 < argc; i += 2)
	{
		if (std::strcmp(argv[i], "--path") == 0)
			path = argv[i + 1];
		else if (std::strcmp(argv[i], "--out") == 0)
			out = argv[i + 1];
	}

	WNDCLASSA wc {};
	wc.lpfnWndProc   = DefWindowProcA;
	wc.hInstance     = GetModuleHandleA(nullptr);
	wc.lpszClassName = "ffspike";
	RegisterClassA(&wc);
	ff::Scene scene = ff::BuildScene();
	HWND hwnd = CreateWindowA("ffspike", "ffspike", WS_OVERLAPPEDWINDOW, 0, 0, scene.width, scene.height, nullptr, nullptr, wc.hInstance, nullptr);

	LoadLibraryA("d3d9.dll");
	IDirect3D9* d3d = path == "9on12warp" ? Create9On12Warp() : Direct3DCreate9(D3D_SDK_VERSION);
	if (d3d == nullptr)
	{
		std::printf("no IDirect3D9 for path '%s'\n", path.c_str());
		return 2;
	}
	PrintAdapter(d3d);

	D3DPRESENT_PARAMETERS pp {};
	pp.Windowed               = TRUE;
	pp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferWidth        = static_cast<UINT>(scene.width);
	pp.BackBufferHeight       = static_cast<UINT>(scene.height);
	pp.BackBufferFormat       = D3DFMT_X8R8G8B8;
	pp.EnableAutoDepthStencil = TRUE;
	pp.AutoDepthStencilFormat = D3DFMT_D24S8;
	pp.hDeviceWindow          = hwnd;
	pp.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;

	IDirect3DDevice9* dev = nullptr;
	const char* vp        = "hardware";
	HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
	if (FAILED(hr))
	{
		vp = "software";
		hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
	}
	if (FAILED(hr))
	{
		std::printf("CreateDevice failed: 0x%08lX\n", static_cast<unsigned long>(hr));
		return 3;
	}
	std::printf("device created (%s vertex processing)\n", vp);

	std::vector<IDirect3DTexture9*> textures;
	for (const ff::Texture& t : scene.textures)
		textures.push_back(Upload(dev, t));

	bool ok = RenderToPng(dev, scene, textures.data(), -1, out);
	std::string stem = out.substr(0, out.size() - 4);
	for (size_t i = 0; i < scene.draws.size(); i++)
		ok &= RenderToPng(dev, scene, textures.data(), static_cast<int>(i), stem + "-" + scene.draws[i].name + ".png");
	std::printf("%s\n", ok ? "wrote images" : "FAILED writing images");

	for (IDirect3DTexture9* t : textures)
		if (t != nullptr)
			t->Release();
	dev->Release();
	d3d->Release();
	DestroyWindow(hwnd);
	return ok ? 0 : 1;
}

#include "Direct3D9_macOS.h"
#include "Cocoa/CocoaApp.h"
#include "Cocoa/MetalRenderer.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>

// Direct3D 9, as much of it as the client uses (port/census/client-census.md), implemented on
// Metal: the device tracks the D3D9 state and turns each draw into a MetalRenderer::DrawDesc.

namespace Platform::macOS
{

namespace
{

using Cocoa::DrawDesc;
using Cocoa::FixedFunctionUniforms;
using Cocoa::MetalRenderer;
using Cocoa::TextureFormat;

// The device's renderer (there is one device).
MetalRenderer* s_pRenderer = nullptr;
std::string s_szSnapshotRequest; // RequestSnapshot()

/// \brief OPENKO_RECORD=<file.mp4> streams what the window shows to ffmpeg, at 30 frames per second
/// of wall-clock time (frames repeat when the game runs slower). A testing aid; needs ffmpeg.
class Recorder
{
public:
	static constexpr int FramesPerSecond = 30;

	Recorder(const std::string& szPath, int iWidth, int iHeight) : _width(iWidth), _height(iHeight)
	{
		// Fragmented MP4 stays playable even if the game is killed mid-recording.
		std::string command = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s "
							  + std::to_string(iWidth) + "x" + std::to_string(iHeight) + " -r "
							  + std::to_string(FramesPerSecond)
							  + " -i - -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p"
								" -movflags frag_keyframe+empty_moov '"
							  + szPath + "'";
		std::string path = "PATH=\"$PATH:/opt/homebrew/bin:/usr/local/bin\" ";
		_pipe            = popen((path + command).c_str(), "w");
		_start           = std::chrono::steady_clock::now();
	}

	~Recorder()
	{
		if (_pipe != nullptr)
			pclose(_pipe);
	}

	Recorder(const Recorder&)            = delete;
	Recorder& operator=(const Recorder&) = delete;

	/// \brief Whether a video frame is due (so the caller reads the next frame back).
	bool Due() const
	{
		return _pipe != nullptr && _written < FramesDue();
	}

	void Write(const std::vector<uint8_t>& bgra, int iWidth, int iHeight)
	{
		if (_pipe == nullptr || iWidth != _width || iHeight != _height)
			return;

		for (int64_t due = FramesDue(); _written < due; _written++)
			std::fwrite(bgra.data(), 1, bgra.size(), _pipe);
	}

private:
	int64_t FramesDue() const
	{
		auto elapsed = std::chrono::steady_clock::now() - _start;
		return (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
				   * FramesPerSecond / 1000)
			   + 1;
	}

	FILE* _pipe      = nullptr;
	int _width       = 0;
	int _height      = 0;
	int64_t _written = 0;
	std::chrono::steady_clock::time_point _start;
};

// Render/texture-stage/sampler state indices the compat header doesn't name.
constexpr DWORD RS_COUNT      = 256;
constexpr DWORD TSS_COUNT     = 33;
constexpr DWORD SAMP_COUNT    = 14;
constexpr DWORD TSS_COLORARG0 = 26;
constexpr DWORD TSS_ALPHAARG0 = 27;
constexpr DWORD MAX_STAGES    = 8;
constexpr DWORD SHADER_STAGES = 3; // the census never goes past stage 2
constexpr DWORD MAX_LIGHTS    = 8;

float AsFloat(DWORD value)
{
	return std::bit_cast<float>(value);
}

void ColorToFloat(D3DCOLOR c, float (&out)[4])
{
	out[0] = ((c >> 16) & 0xFF) / 255.0f;
	out[1] = ((c >> 8) & 0xFF) / 255.0f;
	out[2] = (c & 0xFF) / 255.0f;
	out[3] = (c >> 24) / 255.0f;
}

void ColorValueToFloat(const D3DCOLORVALUE& c, float (&out)[4])
{
	out[0] = c.r;
	out[1] = c.g;
	out[2] = c.b;
	out[3] = c.a;
}

using Matrix = D3DMATRIX;

Matrix Identity()
{
	Matrix m {};
	m._11 = m._22 = m._33 = m._44 = 1.0f;
	return m;
}

Matrix Multiply(const Matrix& a, const Matrix& b)
{
	Matrix r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j]
						+ a.m[i][3] * b.m[3][j];
	return r;
}

Matrix Transpose(const Matrix& a)
{
	Matrix r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = a.m[j][i];
	return r;
}

Matrix Inverse(const Matrix& a)
{
	// Gauss-Jordan with partial pivoting; world-view matrices are well conditioned.
	float m[4][8];
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 8; j++)
			m[i][j] = j < 4 ? a.m[i][j] : (j - 4 == i ? 1.0f : 0.0f);

	for (int c = 0; c < 4; c++)
	{
		int pivot = c;
		for (int r = c + 1; r < 4; r++)
			if (std::fabs(m[r][c]) > std::fabs(m[pivot][c]))
				pivot = r;
		for (int j = 0; j < 8; j++)
			std::swap(m[c][j], m[pivot][j]);

		float d = m[c][c];
		if (d == 0.0f)
			return Identity();
		for (int j = 0; j < 8; j++)
			m[c][j] /= d;

		for (int r = 0; r < 4; r++)
		{
			if (r == c)
				continue;
			float k = m[r][c];
			for (int j = 0; j < 8; j++)
				m[r][j] -= k * m[c][j];
		}
	}

	Matrix r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = m[i][j + 4];
	return r;
}

void TransformPoint(const D3DVECTOR& p, const Matrix& m, float (&out)[4])
{
	out[0] = p.x * m._11 + p.y * m._21 + p.z * m._31 + m._41;
	out[1] = p.x * m._12 + p.y * m._22 + p.z * m._32 + m._42;
	out[2] = p.x * m._13 + p.y * m._23 + p.z * m._33 + m._43;
	out[3] = 1.0f;
}

void TransformDirection(const D3DVECTOR& d, const Matrix& m, float (&out)[4])
{
	out[0] = d.x * m._11 + d.y * m._21 + d.z * m._31;
	out[1] = d.x * m._12 + d.y * m._22 + d.z * m._32;
	out[2] = d.x * m._13 + d.y * m._23 + d.z * m._33;
	out[3] = 0.0f;
}

UINT VertexCount(D3DPRIMITIVETYPE type, UINT primitives)
{
	switch (type)
	{
		case D3DPT_POINTLIST:
			return primitives;
		case D3DPT_LINELIST:
			return primitives * 2;
		case D3DPT_LINESTRIP:
			return primitives + 1;
		case D3DPT_TRIANGLELIST:
			return primitives * 3;
		case D3DPT_TRIANGLESTRIP:
		case D3DPT_TRIANGLEFAN:
			return primitives + 2;
		default:
			return 0;
	}
}

struct FvfLayout
{
	bool rhw        = false;
	uint32_t normal = 0, pointSize = 0, diffuse = 0, specular = 0, tex0 = 0,
			 tex1  = 0; // word offsets
	uint32_t words = 0;
};

FvfLayout LayoutOf(DWORD fvf)
{
	FvfLayout layout;
	uint32_t at = 0;
	switch (fvf & 0x400E) // D3DFVF_POSITION_MASK
	{
		case D3DFVF_XYZRHW:
			layout.rhw = true;
			at         = 4;
			break;
		case 0x4002: // D3DFVF_XYZW
			at = 4;
			break;
		case D3DFVF_XYZ:
			at = 3;
			break;
		default: // D3DFVF_XYZB1..5
			at = 3 + (((fvf & 0x400E) - 0x6) / 2);
			break;
	}

	if (fvf & D3DFVF_NORMAL)
		layout.normal = at, at += 3;
	if (fvf & D3DFVF_PSIZE)
		layout.pointSize = at, at += 1;
	if (fvf & D3DFVF_DIFFUSE)
		layout.diffuse = at, at += 1;
	if (fvf & D3DFVF_SPECULAR)
		layout.specular = at, at += 1;

	uint32_t texCount = (fvf >> 8) & 0xF;
	for (uint32_t i = 0; i < texCount; i++)
	{
		// D3DFVF_TEXCOORDSIZEn: 0 = 2 floats, 1 = 3, 2 = 4, 3 = 1.
		uint32_t sizeBits = (fvf >> (16 + i * 2)) & 0x3;
		uint32_t size     = sizeBits == 0 ? 2 : (sizeBits == 1 ? 3 : (sizeBits == 2 ? 4 : 1));
		if (i == 0)
			layout.tex0 = at;
		else if (i == 1)
			layout.tex1 = at;
		at += size;
	}

	layout.words = at;
	return layout;
}

class MacVertexBuffer : public IDirect3DVertexBuffer9
{
public:
	explicit MacVertexBuffer(UINT length) : _data(length)
	{
	}

	HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD /*Flags*/) override
	{
		if (ppbData == nullptr || OffsetToLock > _data.size()
			|| OffsetToLock + SizeToLock > _data.size())
			return D3DERR_INVALIDCALL;
		*ppbData = std::span(_data).subspan(OffsetToLock).data();
		return D3D_OK;
	}

	HRESULT Unlock() override
	{
		return D3D_OK;
	}

	const std::vector<uint8_t>& Data() const
	{
		return _data;
	}

private:
	std::vector<uint8_t> _data;
};

class MacDevice : public IDirect3DDevice9
{
public:
	explicit MacDevice(const D3DPRESENT_PARAMETERS& params) : _params(params)
	{
		ResetState();

		if (const char* record = std::getenv("OPENKO_RECORD"))
			_recorder = std::make_unique<Recorder>(record,
				static_cast<int>(_params.BackBufferWidth),
				static_cast<int>(_params.BackBufferHeight));

		if (const char* snapshot = std::getenv("OPENKO_SNAPSHOT"))
		{
			std::string spec = snapshot;
			size_t colon     = spec.find(':');
			if (colon != std::string::npos)
			{
				_snapshotFrame = std::strtoul(spec.substr(0, colon).c_str(), nullptr, 10);
				_snapshotPath  = spec.substr(colon + 1);
			}
		}
	}

	~MacDevice() override
	{
		for (IDirect3DBaseTexture9*& texture : _textures)
			SetTextureRef(texture, nullptr);
		if (_streamSource != nullptr)
			_streamSource->Release();
	}

	HRESULT TestCooperativeLevel() override
	{
		return D3D_OK;
	}

	HRESULT GetDeviceCaps(D3DCAPS9* pCaps) override
	{
		if (pCaps == nullptr)
			return D3DERR_INVALIDCALL;

		*pCaps                   = {};
		pCaps->DeviceType        = D3DDEVTYPE_HAL;
		pCaps->PrimitiveMiscCaps = D3DPMISCCAPS_BLENDOP;
		pCaps->TextureCaps       = D3DPTEXTURECAPS_MIPMAP; // no power-of-two or square restrictions
		pCaps->MaxTextureWidth   = 8192;
		pCaps->MaxTextureHeight  = 8192;
		pCaps->MaxTextureBlendStages   = 8;
		pCaps->MaxSimultaneousTextures = 8;
		pCaps->MaxActiveLights         = MAX_LIGHTS;
		pCaps->MaxPointSize            = 64.0f;
		return D3D_OK;
	}

	HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override
	{
		if (pPresentationParameters == nullptr)
			return D3DERR_INVALIDCALL;

		_params = *pPresentationParameters;
		s_pRenderer->Resize(static_cast<int>(_params.BackBufferWidth),
			static_cast<int>(_params.BackBufferHeight),
			_params.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE);
		ResetState();
		return D3D_OK;
	}

	HRESULT Present(const RECT* /*pSourceRect*/, const RECT* /*pDestRect*/,
		HWND /*hDestWindowOverride*/, const RGNDATA* /*pDirtyRegion*/) override
	{
		// OPENKO_SNAPSHOT=<frame>:<file.png> saves that frame (for checking the renderer).
		_frameNumber++;
		if (_snapshotFrame != 0 && _frameNumber == _snapshotFrame)
			s_szSnapshotRequest = _snapshotPath;

		bool bRecord = _recorder != nullptr && _recorder->Due();
		if (!s_szSnapshotRequest.empty() || bRecord)
		{
			std::vector<uint8_t> pixels;
			s_pRenderer->Present(&pixels);
			if (!s_szSnapshotRequest.empty())
			{
				Cocoa::WritePng(
					s_szSnapshotRequest, s_pRenderer->Width(), s_pRenderer->Height(), pixels);
				s_szSnapshotRequest.clear();
			}

			if (bRecord)
				_recorder->Write(pixels, s_pRenderer->Width(), s_pRenderer->Height());
			return D3D_OK;
		}

		s_pRenderer->Present();
		return D3D_OK;
	}

	HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD /*Usage*/, D3DFORMAT Format,
		D3DPOOL /*Pool*/, IDirect3DTexture9** ppTexture, HANDLE* /*pSharedHandle*/) override
	{
		if (ppTexture == nullptr || Width == 0 || Height == 0 || !IsSupportedTextureFormat(Format))
			return D3DERR_INVALIDCALL;

		auto* texture = new MacTexture(Width, Height, Levels, Format);
		if (!texture->IsValid())
		{
			texture->Release();
			return D3DERR_OUTOFVIDEOMEMORY;
		}

		*ppTexture = texture;
		return D3D_OK;
	}

	HRESULT CreateVertexBuffer(UINT Length, DWORD /*Usage*/, DWORD /*FVF*/, D3DPOOL /*Pool*/,
		IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* /*pSharedHandle*/) override
	{
		if (ppVertexBuffer == nullptr || Length == 0)
			return D3DERR_INVALIDCALL;

		*ppVertexBuffer = new MacVertexBuffer(Length);
		return D3D_OK;
	}

	HRESULT BeginScene() override
	{
		return D3D_OK;
	}

	HRESULT EndScene() override
	{
		return D3D_OK;
	}

	HRESULT Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z,
		DWORD /*Stencil*/) override
	{
		Cocoa::ClearDesc desc;
		desc.color = (Flags & D3DCLEAR_TARGET) != 0;
		desc.depth = (Flags & D3DCLEAR_ZBUFFER) != 0;
		ColorToFloat(Color, desc.rgba);
		desc.rgba[3] = 1.0f;
		desc.z       = Z;

		// Clears are confined to the viewport.
		int vl = static_cast<int>(_viewport.X), vt = static_cast<int>(_viewport.Y);
		int vr = vl + static_cast<int>(_viewport.Width),
			vb = vt + static_cast<int>(_viewport.Height);

		if (Count == 0 || pRects == nullptr)
		{
			desc.rect[0] = vl, desc.rect[1] = vt, desc.rect[2] = vr, desc.rect[3] = vb;
			s_pRenderer->Clear(desc);
			return D3D_OK;
		}

		for (const D3DRECT& rect : std::span(pRects, Count))
		{
			desc.rect[0] = std::max(vl, static_cast<int>(rect.x1));
			desc.rect[1] = std::max(vt, static_cast<int>(rect.y1));
			desc.rect[2] = std::min(vr, static_cast<int>(rect.x2));
			desc.rect[3] = std::min(vb, static_cast<int>(rect.y2));
			s_pRenderer->Clear(desc);
		}
		return D3D_OK;
	}

	HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override
	{
		if (pMatrix == nullptr)
			return D3DERR_INVALIDCALL;
		if (Matrix* m = TransformSlot(State))
			*m = *pMatrix;
		return D3D_OK;
	}

	HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override
	{
		if (pMatrix == nullptr)
			return D3DERR_INVALIDCALL;
		Matrix* m = TransformSlot(State);
		*pMatrix  = m != nullptr ? *m : Identity();
		return D3D_OK;
	}

	HRESULT SetViewport(const D3DVIEWPORT9* pViewport) override
	{
		if (pViewport == nullptr)
			return D3DERR_INVALIDCALL;
		_viewport = *pViewport;
		return D3D_OK;
	}

	HRESULT GetViewport(D3DVIEWPORT9* pViewport) override
	{
		if (pViewport == nullptr)
			return D3DERR_INVALIDCALL;
		*pViewport = _viewport;
		return D3D_OK;
	}

	HRESULT SetMaterial(const D3DMATERIAL9* pMaterial) override
	{
		if (pMaterial == nullptr)
			return D3DERR_INVALIDCALL;
		_material = *pMaterial;
		return D3D_OK;
	}

	HRESULT SetLight(DWORD Index, const D3DLIGHT9* pLight) override
	{
		if (pLight == nullptr)
			return D3DERR_INVALIDCALL;
		if (Index >= _lights.size())
		{
			_lights.resize(Index + 1);
			_lightEnabled.resize(Index + 1, FALSE);
		}
		_lights[Index] = *pLight;
		return D3D_OK;
	}

	HRESULT GetLight(DWORD Index, D3DLIGHT9* pLight) override
	{
		if (pLight == nullptr || Index >= _lights.size())
			return D3DERR_INVALIDCALL;
		*pLight = _lights[Index];
		return D3D_OK;
	}

	HRESULT LightEnable(DWORD Index, BOOL Enable) override
	{
		if (Index >= _lights.size())
		{
			// D3D9 creates a default directional light on demand.
			D3DLIGHT9 light {};
			light.Type      = D3DLIGHT_DIRECTIONAL;
			light.Diffuse   = { 1.0f, 1.0f, 1.0f, 0.0f };
			light.Direction = { 0.0f, 0.0f, 1.0f };
			_lights.resize(Index + 1, light);
			_lightEnabled.resize(Index + 1, FALSE);
		}
		_lightEnabled[Index] = Enable;
		return D3D_OK;
	}

	HRESULT GetLightEnable(DWORD Index, BOOL* pEnable) override
	{
		if (pEnable == nullptr || Index >= _lights.size())
			return D3DERR_INVALIDCALL;
		*pEnable = _lightEnabled[Index];
		return D3D_OK;
	}

	HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override
	{
		if (State >= RS_COUNT)
			return D3DERR_INVALIDCALL;
		_renderStates[State] = Value;
		return D3D_OK;
	}

	HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override
	{
		if (State >= RS_COUNT || pValue == nullptr)
			return D3DERR_INVALIDCALL;
		*pValue = _renderStates[State];
		return D3D_OK;
	}

	HRESULT SetClipStatus(const D3DCLIPSTATUS9* /*pClipStatus*/) override
	{
		return D3D_OK;
	}

	HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override
	{
		if (Stage >= MAX_STAGES)
			return D3DERR_INVALIDCALL;
		SetTextureRef(_textures[Stage], pTexture);
		return D3D_OK;
	}

	HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) override
	{
		if (Stage >= MAX_STAGES || Type >= TSS_COUNT || pValue == nullptr)
			return D3DERR_INVALIDCALL;
		*pValue = _stageStates[Stage][Type];
		return D3D_OK;
	}

	HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override
	{
		if (Stage >= MAX_STAGES || Type >= TSS_COUNT)
			return D3DERR_INVALIDCALL;
		_stageStates[Stage][Type] = Value;
		return D3D_OK;
	}

	HRESULT GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) override
	{
		if (Sampler >= MAX_STAGES || Type >= SAMP_COUNT || pValue == nullptr)
			return D3DERR_INVALIDCALL;
		*pValue = _samplerStates[Sampler][Type];
		return D3D_OK;
	}

	HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override
	{
		if (Sampler >= MAX_STAGES || Type >= SAMP_COUNT)
			return D3DERR_INVALIDCALL;
		_samplerStates[Sampler][Type] = Value;
		return D3D_OK;
	}

	HRESULT ValidateDevice(DWORD* pNumPasses) override
	{
		if (pNumPasses != nullptr)
			*pNumPasses = 1;
		return D3D_OK;
	}

	HRESULT SetScissorRect(const RECT* pRect) override
	{
		if (pRect == nullptr)
			return D3DERR_INVALIDCALL;
		_scissor = *pRect;
		return D3D_OK;
	}

	HRESULT DrawPrimitive(
		D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override
	{
		auto* vb = static_cast<MacVertexBuffer*>(_streamSource);
		if (vb == nullptr || _streamStride == 0)
			return D3DERR_INVALIDCALL;

		UINT count   = VertexCount(PrimitiveType, PrimitiveCount);
		size_t start = _streamOffset + static_cast<size_t>(StartVertex) * _streamStride;
		size_t bytes = static_cast<size_t>(count) * _streamStride;
		if (count == 0 || start + bytes > vb->Data().size())
			return D3DERR_INVALIDCALL;

		Submit(PrimitiveType, PrimitiveCount, std::span(vb->Data()).subspan(start).data(), bytes,
			_streamStride, count, nullptr, 0);
		return D3D_OK;
	}

	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE /*PrimitiveType*/, INT /*BaseVertexIndex*/,
		UINT /*MinVertexIndex*/, UINT /*NumVertices*/, UINT /*startIndex*/,
		UINT /*primCount*/) override
	{
		// Index buffers can't be created through this device (the client never does), so nothing
		// can be bound for this to draw from.
		return D3DERR_INVALIDCALL;
	}

	HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
		const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override
	{
		UINT count = VertexCount(PrimitiveType, PrimitiveCount);
		if (pVertexStreamZeroData == nullptr || count == 0 || VertexStreamZeroStride == 0)
			return D3DERR_INVALIDCALL;

		Submit(PrimitiveType, PrimitiveCount, pVertexStreamZeroData,
			static_cast<size_t>(count) * VertexStreamZeroStride, VertexStreamZeroStride, count,
			nullptr, 0);
		return D3D_OK;
	}

	HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
		UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat,
		const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) override
	{
		UINT indexCount = VertexCount(PrimitiveType, PrimitiveCount);
		if (pIndexData == nullptr || pVertexStreamZeroData == nullptr || indexCount == 0
			|| VertexStreamZeroStride == 0)
			return D3DERR_INVALIDCALL;

		_indices.resize(indexCount);
		if (IndexDataFormat == D3DFMT_INDEX16)
		{
			std::memcpy(_indices.data(), pIndexData, indexCount * sizeof(uint16_t));
		}
		else
		{
			std::span indices32(static_cast<const uint32_t*>(pIndexData), indexCount);
			for (UINT i = 0; i < indexCount; i++)
				_indices[i] = static_cast<uint16_t>(indices32[i]);
		}

		size_t bytes = static_cast<size_t>(MinVertexIndex + NumVertices) * VertexStreamZeroStride;
		Submit(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, bytes, VertexStreamZeroStride,
			0, _indices.data(), indexCount);
		return D3D_OK;
	}

	HRESULT SetFVF(DWORD FVF) override
	{
		_fvf = FVF;
		return D3D_OK;
	}

	HRESULT GetFVF(DWORD* pFVF) override
	{
		if (pFVF == nullptr)
			return D3DERR_INVALIDCALL;
		*pFVF = _fvf;
		return D3D_OK;
	}

	HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData,
		UINT OffsetInBytes, UINT Stride) override
	{
		if (StreamNumber != 0)
			return D3DERR_INVALIDCALL;

		if (pStreamData != nullptr)
			pStreamData->AddRef();
		if (_streamSource != nullptr)
			_streamSource->Release();

		_streamSource = pStreamData;
		_streamOffset = OffsetInBytes;
		_streamStride = Stride;
		return D3D_OK;
	}

	HRESULT SetIndices(IDirect3DIndexBuffer9* /*pIndexData*/) override
	{
		return D3D_OK;
	}

private:
	void ResetState()
	{
		std::fill(std::begin(_renderStates), std::end(_renderStates), 0u);
		_renderStates[D3DRS_ZENABLE]   = _params.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
		_renderStates[D3DRS_FILLMODE]  = D3DFILL_SOLID;
		_renderStates[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
		_renderStates[D3DRS_ZWRITEENABLE]           = TRUE;
		_renderStates[D3DRS_SRCBLEND]               = D3DBLEND_ONE;
		_renderStates[D3DRS_DESTBLEND]              = D3DBLEND_ZERO;
		_renderStates[D3DRS_CULLMODE]               = D3DCULL_CCW;
		_renderStates[D3DRS_ZFUNC]                  = D3DCMP_LESSEQUAL;
		_renderStates[D3DRS_ALPHAFUNC]              = D3DCMP_ALWAYS;
		_renderStates[D3DRS_FOGSTART]               = std::bit_cast<DWORD>(0.0f);
		_renderStates[D3DRS_FOGEND]                 = std::bit_cast<DWORD>(1.0f);
		_renderStates[D3DRS_FOGDENSITY]             = std::bit_cast<DWORD>(1.0f);
		_renderStates[D3DRS_TEXTUREFACTOR]          = 0xFFFFFFFF;
		_renderStates[D3DRS_LIGHTING]               = TRUE;
		_renderStates[D3DRS_COLORVERTEX]            = TRUE;
		_renderStates[D3DRS_LOCALVIEWER]            = TRUE;
		_renderStates[D3DRS_DIFFUSEMATERIALSOURCE]  = D3DMCS_COLOR1;
		_renderStates[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
		_renderStates[D3DRS_AMBIENTMATERIALSOURCE]  = D3DMCS_MATERIAL;
		_renderStates[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
		_renderStates[D3DRS_POINTSIZE]              = std::bit_cast<DWORD>(1.0f);
		_renderStates[D3DRS_BLENDOP]                = D3DBLENDOP_ADD;

		for (DWORD stage = 0; stage < MAX_STAGES; stage++)
		{
			auto& tss = _stageStates[stage];
			std::fill(std::begin(tss), std::end(tss), 0u);
			tss[D3DTSS_COLOROP]       = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
			tss[D3DTSS_ALPHAOP]       = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
			tss[D3DTSS_COLORARG1]     = D3DTA_TEXTURE;
			tss[D3DTSS_COLORARG2]     = D3DTA_CURRENT;
			tss[D3DTSS_ALPHAARG1]     = D3DTA_TEXTURE;
			tss[D3DTSS_ALPHAARG2]     = D3DTA_CURRENT;
			tss[D3DTSS_TEXCOORDINDEX] = stage;
			tss[TSS_COLORARG0]        = D3DTA_CURRENT;
			tss[TSS_ALPHAARG0]        = D3DTA_CURRENT;

			auto& samp                = _samplerStates[stage];
			std::fill(std::begin(samp), std::end(samp), 0u);
			samp[D3DSAMP_ADDRESSU]  = D3DTADDRESS_WRAP;
			samp[D3DSAMP_ADDRESSV]  = D3DTADDRESS_WRAP;
			samp[D3DSAMP_ADDRESSW]  = D3DTADDRESS_WRAP;
			samp[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
			samp[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
			samp[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;

			SetTextureRef(_textures[stage], nullptr);
		}

		_world      = Identity();
		_view       = Identity();
		_projection = Identity();
		_material   = {};
		_lights.clear();
		_lightEnabled.clear();
		_viewport = { 0, 0, _params.BackBufferWidth, _params.BackBufferHeight, 0.0f, 1.0f };
		_scissor  = { 0, 0, static_cast<LONG>(_params.BackBufferWidth),
			 static_cast<LONG>(_params.BackBufferHeight) };
		_fvf      = 0;
	}

	Matrix* TransformSlot(D3DTRANSFORMSTATETYPE state)
	{
		switch (state)
		{
			case D3DTS_VIEW:
				return &_view;
			case D3DTS_PROJECTION:
				return &_projection;
			default:
				return state == D3DTS_WORLD ? &_world : nullptr; // texture transforms aren't used
		}
	}

	static void SetTextureRef(IDirect3DBaseTexture9*& slot, IDirect3DBaseTexture9* texture)
	{
		if (texture != nullptr)
			texture->AddRef();
		if (slot != nullptr)
			slot->Release();
		slot = texture;
	}

	// The material colour each lighting term takes: 0 material, 1 vertex diffuse, 2 vertex specular.
	uint32_t MaterialSource(DWORD state, const FvfLayout& layout) const
	{
		if (!_renderStates[D3DRS_COLORVERTEX])
			return 0;

		DWORD source = _renderStates[state];
		if (source == D3DMCS_COLOR1 && (_fvf & D3DFVF_DIFFUSE))
			return 1;
		if (source == D3DMCS_COLOR2 && (_fvf & D3DFVF_SPECULAR))
			return 2;
		(void) layout;
		return 0;
	}

	void BuildUniforms(FixedFunctionUniforms& u, const FvfLayout& layout, UINT stride) const
	{
		const auto& rs = _renderStates;

		std::memcpy(u.world, &_world, sizeof(u.world));
		std::memcpy(u.view, &_view, sizeof(u.view));
		std::memcpy(u.proj, &_projection, sizeof(u.proj));

		bool lighting = rs[D3DRS_LIGHTING] != 0 && !layout.rhw;
		Matrix normal = lighting ? Transpose(Inverse(Multiply(_world, _view))) : Identity();
		std::memcpy(u.normalMat, &normal, sizeof(u.normalMat));

		u.viewport[0]    = static_cast<float>(_viewport.X);
		u.viewport[1]    = static_cast<float>(_viewport.Y);
		u.viewport[2]    = static_cast<float>(_viewport.Width);
		u.viewport[3]    = static_cast<float>(_viewport.Height);
		u.viewportInv[0] = _viewport.Width > 0 ? 1.0f / static_cast<float>(_viewport.Width) : 0.0f;
		u.viewportInv[1] = _viewport.Height > 0 ? 1.0f / static_cast<float>(_viewport.Height)
												: 0.0f;

		u.v0[0]          = _fvf;
		u.v0[1]          = stride / 4;
		u.v0[2]          = layout.normal;
		u.v0[3]          = layout.diffuse;
		u.v1[0]          = layout.specular;
		u.v1[1]          = layout.tex0;
		u.v1[2]          = layout.tex1;
		u.v1[3]          = layout.pointSize;
		u.v2[0]          = lighting ? 1 : 0;
		u.v2[1]          = rs[D3DRS_SPECULARENABLE] != 0 ? 1 : 0;
		u.v2[2]          = rs[D3DRS_LOCALVIEWER] != 0 ? 1 : 0;
		u.v2[3]          = rs[D3DRS_NORMALIZENORMALS] != 0 ? 1 : 0;

		// Table fog wins over vertex fog; it's computed from the depth (w), vertex fog from the eye
		// distance (range fog) or eye z.
		DWORD tableFog   = rs[D3DRS_FOGTABLEMODE];
		DWORD vertexFog  = rs[D3DRS_FOGVERTEXMODE];
		DWORD fogMode    = tableFog != D3DFOG_NONE ? tableFog : vertexFog;
		u.v3[0]          = rs[D3DRS_FOGENABLE] != 0 && fogMode != D3DFOG_NONE ? 1 : 0;
		u.v3[1]          = fogMode;
		u.v3[2]          = rs[D3DRS_ALPHATESTENABLE] != 0 ? rs[D3DRS_ALPHAFUNC] : 0;
		u.v3[3]          = rs[D3DRS_ALPHAREF] & 0xFF;
		u.p1[1] = tableFog != D3DFOG_NONE ? 0.0f : (rs[D3DRS_RANGEFOGENABLE] != 0 ? 1.0f : 2.0f);

		u.v4[0] = MaterialSource(D3DRS_DIFFUSEMATERIALSOURCE, layout);
		u.v4[1] = MaterialSource(D3DRS_AMBIENTMATERIALSOURCE, layout);
		u.v4[2] = MaterialSource(D3DRS_SPECULARMATERIALSOURCE, layout);
		u.v4[3] = MaterialSource(D3DRS_EMISSIVEMATERIALSOURCE, layout);

		ColorToFloat(rs[D3DRS_AMBIENT], u.ambient);
		ColorValueToFloat(_material.Diffuse, u.matDiffuse);
		ColorValueToFloat(_material.Ambient, u.matAmbient);
		ColorValueToFloat(_material.Specular, u.matSpecular);
		ColorValueToFloat(_material.Emissive, u.matEmissive);
		u.p0[0] = _material.Power;
		u.p0[1] = AsFloat(rs[D3DRS_FOGDENSITY]);
		u.p0[2] = AsFloat(rs[D3DRS_FOGSTART]);
		u.p0[3] = AsFloat(rs[D3DRS_FOGEND]);
		u.p1[0] = AsFloat(rs[D3DRS_POINTSIZE]);
		ColorToFloat(rs[D3DRS_FOGCOLOR], u.fogColor);
		ColorToFloat(rs[D3DRS_TEXTUREFACTOR], u.tfactor);

		if (lighting)
		{
			DWORD slot = 0;
			for (size_t i = 0; i < _lights.size() && slot < MAX_LIGHTS; i++)
			{
				if (!_lightEnabled[i])
					continue;

				const D3DLIGHT9& light               = _lights[i];
				FixedFunctionUniforms::Light& target = u.lights[slot++];
				ColorValueToFloat(light.Diffuse, target.diffuse);
				ColorValueToFloat(light.Specular, target.specular);
				ColorValueToFloat(light.Ambient, target.ambient);
				TransformPoint(light.Position, _view, target.position);
				TransformDirection(light.Direction, _view, target.direction);
				target.params[0] = light.Type == D3DLIGHT_DIRECTIONAL ? 0.0f : light.Range;
				target.params[1] = light.Attenuation0;
				target.params[2] = light.Attenuation1;
				target.params[3] = light.Attenuation2;
				target.spot[0]   = light.Falloff;
				target.spot[1]   = std::cos(light.Theta / 2.0f);
				target.spot[2]   = std::cos(light.Phi / 2.0f);
				target.info[0]   = light.Type;
			}
		}

		bool disabled = false;
		for (DWORD stage = 0; stage < SHADER_STAGES; stage++)
		{
			const auto& tss                      = _stageStates[stage];
			const auto& samp                     = _samplerStates[stage];
			FixedFunctionUniforms::Stage& target = u.stages[stage];
			if (tss[D3DTSS_COLOROP] == D3DTOP_DISABLE)
				disabled = true;

			auto* texture = static_cast<MacTexture*>(_textures[stage]);
			target.a[0]   = disabled ? static_cast<uint32_t>(D3DTOP_DISABLE) : tss[D3DTSS_COLOROP];
			target.a[1]   = tss[D3DTSS_COLORARG1];
			target.a[2]   = tss[D3DTSS_COLORARG2];
			target.a[3]   = disabled ? static_cast<uint32_t>(D3DTOP_DISABLE) : tss[D3DTSS_ALPHAOP];
			target.b[0]   = tss[D3DTSS_ALPHAARG1];
			target.b[1]   = tss[D3DTSS_ALPHAARG2];
			target.b[2]   = texture != nullptr ? 1 : 0;
			target.b[3]   = tss[D3DTSS_TEXCOORDINDEX] & 0xFFFF;
			target.c[0]   = samp[D3DSAMP_MIPFILTER] != D3DTEXF_NONE && texture != nullptr
                                  && texture->GetLevelCount() > 1
								? 1
								: 0;
			target.c[1]   = samp[D3DSAMP_MINFILTER] == D3DTEXF_POINT
                                  && samp[D3DSAMP_MAGFILTER] == D3DTEXF_POINT
                                  && samp[D3DSAMP_MIPFILTER] == D3DTEXF_NONE
								? 1
								: 0;
			target.c[2]   = (samp[D3DSAMP_ADDRESSU] == D3DTADDRESS_BORDER ? 1u : 0u)
						  | (samp[D3DSAMP_ADDRESSV] == D3DTADDRESS_BORDER ? 2u : 0u);
			target.c[3]      = samp[D3DSAMP_BORDERCOLOR];
			target.d[0]      = tss[TSS_COLORARG0];
			target.d[1]      = tss[TSS_ALPHAARG0];
			u.lodBias[stage] = AsFloat(samp[D3DSAMP_MIPMAPLODBIAS]);
		}
	}

	void Submit(D3DPRIMITIVETYPE type, UINT primitives, const void* vertices, size_t vertexBytes,
		UINT stride, UINT vertexCount, const uint16_t* indices, UINT indexCount)
	{
		FvfLayout layout = LayoutOf(_fvf);

		DrawDesc& desc   = _draw;
		desc             = DrawDesc {};
		BuildUniforms(desc.uniforms, layout, stride);

		const auto& rs      = _renderStates;
		desc.blend          = rs[D3DRS_ALPHABLENDENABLE] != 0;
		desc.srcBlend       = rs[D3DRS_SRCBLEND];
		desc.destBlend      = rs[D3DRS_DESTBLEND];
		desc.blendOp        = rs[D3DRS_BLENDOP];
		desc.depthTest      = rs[D3DRS_ZENABLE] != D3DZB_FALSE;
		desc.depthWrite     = rs[D3DRS_ZWRITEENABLE] != 0;
		desc.depthFunc      = rs[D3DRS_ZFUNC];
		desc.cullMode       = rs[D3DRS_CULLMODE];
		desc.wireframe      = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME;
		desc.scissor        = rs[D3DRS_SCISSORTESTENABLE] != 0;
		desc.scissorRect[0] = _scissor.left;
		desc.scissorRect[1] = _scissor.top;
		desc.scissorRect[2] = _scissor.right;
		desc.scissorRect[3] = _scissor.bottom;
		desc.viewport[0]    = static_cast<float>(_viewport.X);
		desc.viewport[1]    = static_cast<float>(_viewport.Y);
		desc.viewport[2]    = static_cast<float>(_viewport.Width);
		desc.viewport[3]    = static_cast<float>(_viewport.Height);
		desc.viewport[4]    = _viewport.MinZ;
		desc.viewport[5]    = _viewport.MaxZ;

		for (DWORD stage = 0; stage < SHADER_STAGES; stage++)
		{
			auto* texture                  = static_cast<MacTexture*>(_textures[stage]);
			const auto& samp               = _samplerStates[stage];
			desc.textures[stage]           = texture != nullptr ? texture->Handle() : nullptr;
			desc.samplers[stage].minFilter = samp[D3DSAMP_MINFILTER];
			desc.samplers[stage].magFilter = samp[D3DSAMP_MAGFILTER];
			desc.samplers[stage].mipFilter = texture != nullptr && texture->GetLevelCount() > 1
												 ? samp[D3DSAMP_MIPFILTER]
												 : D3DTEXF_NONE;
			desc.samplers[stage].addressU  = samp[D3DSAMP_ADDRESSU];
			desc.samplers[stage].addressV  = samp[D3DSAMP_ADDRESSV];
		}

		desc.vertices    = vertices;
		desc.vertexBytes = vertexBytes;

		// Metal has no triangle fans: draw them as lists.
		if (type == D3DPT_TRIANGLEFAN)
		{
			_fanIndices.clear();
			_fanIndices.reserve(static_cast<size_t>(primitives) * 3);
			std::span<const uint16_t> source(indices, indices != nullptr ? primitives + 2 : 0);
			for (UINT i = 0; i < primitives; i++)
			{
				uint16_t a = source.empty() ? 0 : source[0];
				uint16_t b = source.empty() ? static_cast<uint16_t>(i + 1) : source[i + 1];
				uint16_t c = source.empty() ? static_cast<uint16_t>(i + 2) : source[i + 2];
				_fanIndices.insert(_fanIndices.end(), { a, b, c });
			}

			desc.primitiveType = D3DPT_TRIANGLELIST;
			desc.indices       = _fanIndices.data();
			desc.indexCount    = static_cast<uint32_t>(_fanIndices.size());
		}
		else
		{
			desc.primitiveType = type;
			desc.indices       = indices;
			desc.indexCount    = indexCount;
			desc.vertexCount   = vertexCount;
		}

		s_pRenderer->Draw(desc);
	}

	D3DPRESENT_PARAMETERS _params;
	DWORD _renderStates[RS_COUNT] {};
	DWORD _stageStates[MAX_STAGES][TSS_COUNT] {};
	DWORD _samplerStates[MAX_STAGES][SAMP_COUNT] {};
	IDirect3DBaseTexture9* _textures[MAX_STAGES] {};
	Matrix _world {}, _view {}, _projection {};
	D3DMATERIAL9 _material {};
	std::vector<D3DLIGHT9> _lights;
	std::vector<BOOL> _lightEnabled;
	D3DVIEWPORT9 _viewport {};
	RECT _scissor {};
	DWORD _fvf                            = 0;

	IDirect3DVertexBuffer9* _streamSource = nullptr;
	UINT _streamOffset = 0, _streamStride = 0;

	DrawDesc _draw;
	std::vector<uint16_t> _indices, _fanIndices;

	unsigned long _frameNumber   = 0;
	unsigned long _snapshotFrame = 0;
	std::string _snapshotPath;
	std::unique_ptr<Recorder> _recorder;
};

class MacDirect3D9 : public IDirect3D9
{
public:
	UINT GetAdapterModeCount(UINT /*Adapter*/, D3DFORMAT Format) override
	{
		return Format == D3DFMT_X8R8G8B8 ? static_cast<UINT>(std::size(Modes)) : 0;
	}

	HRESULT EnumAdapterModes(
		UINT /*Adapter*/, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) override
	{
		if (Format != D3DFMT_X8R8G8B8 || Mode >= std::size(Modes) || pMode == nullptr)
			return D3DERR_INVALIDCALL;
		*pMode = { Modes[Mode][0], Modes[Mode][1], 60, D3DFMT_X8R8G8B8 };
		return D3D_OK;
	}

	HRESULT GetAdapterDisplayMode(UINT /*Adapter*/, D3DDISPLAYMODE* pMode) override
	{
		if (pMode == nullptr)
			return D3DERR_INVALIDCALL;
		*pMode = { 1920, 1080, 60, D3DFMT_X8R8G8B8 };
		return D3D_OK;
	}

	HRESULT CheckDeviceFormat(UINT /*Adapter*/, D3DDEVTYPE /*DeviceType*/,
		D3DFORMAT /*AdapterFormat*/, DWORD Usage, D3DRESOURCETYPE RType,
		D3DFORMAT CheckFormat) override
	{
		if (Usage & D3DUSAGE_DEPTHSTENCIL)
			return CheckFormat == D3DFMT_D24S8 || CheckFormat == D3DFMT_D24X8
						   || CheckFormat == D3DFMT_D16
					   ? D3D_OK
					   : D3DERR_INVALIDCALL;
		if (RType == D3DRTYPE_TEXTURE)
			return IsSupportedTextureFormat(CheckFormat) ? D3D_OK : D3DERR_INVALIDCALL;
		return D3D_OK;
	}

	HRESULT CheckDepthStencilMatch(UINT /*Adapter*/, D3DDEVTYPE /*DeviceType*/,
		D3DFORMAT /*AdapterFormat*/, D3DFORMAT /*RenderTargetFormat*/,
		D3DFORMAT DepthStencilFormat) override
	{
		return DepthStencilFormat == D3DFMT_D24S8 || DepthStencilFormat == D3DFMT_D24X8
					   || DepthStencilFormat == D3DFMT_D16
				   ? D3D_OK
				   : D3DERR_INVALIDCALL;
	}

	HRESULT CreateDevice(UINT /*Adapter*/, D3DDEVTYPE /*DeviceType*/, HWND /*hFocusWindow*/,
		DWORD /*BehaviorFlags*/, D3DPRESENT_PARAMETERS* pPresentationParameters,
		IDirect3DDevice9** ppReturnedDeviceInterface) override
	{
		if (pPresentationParameters == nullptr || ppReturnedDeviceInterface == nullptr
			|| s_pRenderer != nullptr)
			return D3DERR_INVALIDCALL;

		s_pRenderer = MetalRenderer::Create(Cocoa::GameLayer(),
			static_cast<int>(pPresentationParameters->BackBufferWidth),
			static_cast<int>(pPresentationParameters->BackBufferHeight),
			pPresentationParameters->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE);
		if (s_pRenderer == nullptr)
			return D3DERR_INVALIDCALL;

		*ppReturnedDeviceInterface = new MacDevice(*pPresentationParameters);
		return D3D_OK;
	}

private:
	static constexpr UINT Modes[][2] = { { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1280, 800 },
		{ 1280, 1024 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 }, { 1600, 1200 }, { 1680, 1050 },
		{ 1920, 1080 }, { 1920, 1200 } };
};

} // namespace

void RequestSnapshot(const std::string& szPath)
{
	s_szSnapshotRequest = szPath;
}

// --- formats ------------------------------------------------------------------------------

bool IsCompressedFormat(D3DFORMAT format)
{
	return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3
		   || format == D3DFMT_DXT4 || format == D3DFMT_DXT5;
}

bool IsSupportedTextureFormat(D3DFORMAT format)
{
	switch (format)
	{
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_X1R5G5B5:
		case D3DFMT_R5G6B5:
			return true;
		default:
			return IsCompressedFormat(format);
	}
}

size_t LevelPitch(D3DFORMAT format, UINT width)
{
	switch (format)
	{
		case D3DFMT_DXT1:
			return ((width + 3) / 4) * 8;
		case D3DFMT_DXT2:
		case D3DFMT_DXT3:
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
			return ((width + 3) / 4) * 16;
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
			return static_cast<size_t>(width) * 4;
		default:
			return static_cast<size_t>(width) * 2;
	}
}

UINT LevelRows(D3DFORMAT format, UINT height)
{
	return IsCompressedFormat(format) ? (height + 3) / 4 : height;
}

namespace
{

uint8_t Expand5(uint32_t v)
{
	return static_cast<uint8_t>((v << 3) | (v >> 2));
}

uint8_t Expand6(uint32_t v)
{
	return static_cast<uint8_t>((v << 2) | (v >> 4));
}

uint32_t Quantize(uint8_t v, uint32_t maxValue)
{
	return (v * maxValue + 127) / 255;
}

} // namespace

void ToBGRA8(D3DFORMAT format, const uint8_t* src, size_t srcPitch, UINT width, UINT height,
	std::vector<uint8_t>& out)
{
	std::span<const uint8_t> in(src, srcPitch * height);
	out.resize(static_cast<size_t>(width) * height * 4);
	for (UINT y = 0; y < height; y++)
	{
		size_t row = y * srcPitch;
		size_t o   = static_cast<size_t>(y) * width * 4;
		for (UINT x = 0; x < width; x++, o += 4)
		{
			if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8)
			{
				for (size_t c = 0; c < 4; c++)
					out[o + c] = in[row + (x * 4) + c];
				if (format == D3DFMT_X8R8G8B8)
					out[o + 3] = 255;
				continue;
			}

			uint32_t v = static_cast<uint32_t>(in[row + (x * 2)] | (in[row + (x * 2) + 1] << 8));
			switch (format)
			{
				case D3DFMT_A4R4G4B4:
					out[o + 0] = static_cast<uint8_t>((v & 0xF) * 17);
					out[o + 1] = static_cast<uint8_t>(((v >> 4) & 0xF) * 17);
					out[o + 2] = static_cast<uint8_t>(((v >> 8) & 0xF) * 17);
					out[o + 3] = static_cast<uint8_t>(((v >> 12) & 0xF) * 17);
					break;
				case D3DFMT_A1R5G5B5:
				case D3DFMT_X1R5G5B5:
					out[o + 0] = Expand5(v & 0x1F);
					out[o + 1] = Expand5((v >> 5) & 0x1F);
					out[o + 2] = Expand5((v >> 10) & 0x1F);
					out[o + 3] = format == D3DFMT_X1R5G5B5 || (v & 0x8000) != 0 ? 255 : 0;
					break;
				default: // D3DFMT_R5G6B5
					out[o + 0] = Expand5(v & 0x1F);
					out[o + 1] = Expand6((v >> 5) & 0x3F);
					out[o + 2] = Expand5((v >> 11) & 0x1F);
					out[o + 3] = 255;
					break;
			}
		}
	}
}

void FromBGRA8(
	D3DFORMAT format, const uint8_t* src, UINT width, UINT height, uint8_t* dest, size_t destPitch)
{
	std::span<const uint8_t> in(src, static_cast<size_t>(width) * height * 4);
	std::span<uint8_t> out(dest, destPitch * height);
	for (UINT y = 0; y < height; y++)
	{
		size_t s   = static_cast<size_t>(y) * width * 4;
		size_t row = y * destPitch;
		for (UINT x = 0; x < width; x++, s += 4)
		{
			if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8)
			{
				for (size_t c = 0; c < 4; c++)
					out[row + (x * 4) + c] = in[s + c];
				continue;
			}

			uint32_t v = 0;
			switch (format)
			{
				case D3DFMT_A4R4G4B4:
					v = Quantize(in[s], 15) | (Quantize(in[s + 1], 15) << 4)
						| (Quantize(in[s + 2], 15) << 8) | (Quantize(in[s + 3], 15) << 12);
					break;
				case D3DFMT_A1R5G5B5:
				case D3DFMT_X1R5G5B5:
					v = Quantize(in[s], 31) | (Quantize(in[s + 1], 31) << 5)
						| (Quantize(in[s + 2], 31) << 10) | (in[s + 3] >= 128 ? 0x8000u : 0u);
					break;
				default: // D3DFMT_R5G6B5
					v = Quantize(in[s], 31) | (Quantize(in[s + 1], 63) << 5)
						| (Quantize(in[s + 2], 31) << 11);
					break;
			}
			out[row + (x * 2)]     = static_cast<uint8_t>(v & 0xFF);
			out[row + (x * 2) + 1] = static_cast<uint8_t>(v >> 8);
		}
	}
}

// --- textures -----------------------------------------------------------------------------

MacTexture::MacTexture(UINT width, UINT height, UINT levels, D3DFORMAT format) :
	_width(width), _height(height), _levels(levels), _format(format)
{
	if (_levels == 0)
	{
		_levels = 1;
		for (UINT size = std::max(width, height); size > 1; size >>= 1)
			_levels++;
	}

	TextureFormat backend = TextureFormat::BGRA8;
	if (format == D3DFMT_DXT1)
		backend = TextureFormat::BC1;
	else if (format == D3DFMT_DXT2 || format == D3DFMT_DXT3)
		backend = TextureFormat::BC2;
	else if (format == D3DFMT_DXT4 || format == D3DFMT_DXT5)
		backend = TextureFormat::BC3;

	_data.resize(_levels);
	if (s_pRenderer != nullptr)
		_handle = s_pRenderer->CreateTexture(width, height, _levels, backend);
}

MacTexture::~MacTexture()
{
	if (_handle != nullptr && s_pRenderer != nullptr)
		s_pRenderer->DestroyTexture(_handle);
}

DWORD MacTexture::GetLevelCount()
{
	return _levels;
}

UINT MacTexture::LevelWidth(UINT level) const
{
	return std::max(1u, _width >> level);
}

UINT MacTexture::LevelHeight(UINT level) const
{
	return std::max(1u, _height >> level);
}

HRESULT MacTexture::GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc)
{
	if (pDesc == nullptr || Level >= _levels)
		return D3DERR_INVALIDCALL;

	*pDesc        = {};
	pDesc->Format = _format;
	pDesc->Type   = D3DRTYPE_SURFACE;
	pDesc->Pool   = D3DPOOL_MANAGED;
	pDesc->Width  = LevelWidth(Level);
	pDesc->Height = LevelHeight(Level);
	return D3D_OK;
}

HRESULT MacTexture::GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel)
{
	if (ppSurfaceLevel == nullptr || Level >= _levels)
		return D3DERR_INVALIDCALL;

	*ppSurfaceLevel = new MacSurface(this, Level);
	return D3D_OK;
}

std::vector<uint8_t>& MacTexture::LevelData(UINT level)
{
	std::vector<uint8_t>& data = _data[level];
	if (data.empty())
		data.resize(
			LevelPitch(_format, LevelWidth(level)) * LevelRows(_format, LevelHeight(level)));
	return data;
}

HRESULT MacTexture::LockRect(
	UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD /*Flags*/)
{
	if (pLockedRect == nullptr || Level >= _levels)
		return D3DERR_INVALIDCALL;

	std::vector<uint8_t>& data = LevelData(Level);
	size_t pitch               = LevelPitch(_format, LevelWidth(Level));
	size_t offset              = 0;
	if (pRect != nullptr)
	{
		bool compressed = IsCompressedFormat(_format);
		size_t x        = compressed ? pRect->left / 4 : pRect->left;
		size_t y        = compressed ? pRect->top / 4 : pRect->top;
		size_t texel = compressed ? (_format == D3DFMT_DXT1 ? 8 : 16) : (pitch / LevelWidth(Level));
		offset       = y * pitch + x * texel;
	}

	pLockedRect->Pitch = static_cast<INT>(pitch);
	pLockedRect->pBits = std::span(data).subspan(offset).data();
	return D3D_OK;
}

HRESULT MacTexture::UnlockRect(UINT Level)
{
	if (Level >= _levels)
		return D3DERR_INVALIDCALL;

	Upload(Level);
	return D3D_OK;
}

void MacTexture::Upload(UINT level)
{
	if (_handle == nullptr || s_pRenderer == nullptr)
		return;

	std::vector<uint8_t>& data = LevelData(level);
	UINT w = LevelWidth(level), h = LevelHeight(level);
	if (IsCompressedFormat(_format))
	{
		s_pRenderer->UploadTexture(_handle, level, data.data(), LevelPitch(_format, w));
		// Compressed textures are never read back; don't keep a second copy of them.
		std::vector<uint8_t>().swap(data);
		return;
	}

	std::vector<uint8_t> bgra;
	ToBGRA8(_format, data.data(), LevelPitch(_format, w), w, h, bgra);
	s_pRenderer->UploadTexture(_handle, level, bgra.data(), static_cast<size_t>(w) * 4);
}

MacSurface::MacSurface(MacTexture* texture, UINT level) : _texture(texture), _level(level)
{
	_texture->AddRef();
}

MacSurface::~MacSurface()
{
	_texture->Release();
}

HRESULT MacSurface::GetDesc(D3DSURFACE_DESC* pDesc)
{
	return _texture->GetLevelDesc(_level, pDesc);
}

HRESULT MacSurface::LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags)
{
	return _texture->LockRect(_level, pLockedRect, pRect, Flags);
}

HRESULT MacSurface::UnlockRect()
{
	return _texture->UnlockRect(_level);
}

} // namespace Platform::macOS

IDirect3D9* Direct3DCreate9(UINT /*SDKVersion*/)
{
	return new Platform::macOS::MacDirect3D9();
}

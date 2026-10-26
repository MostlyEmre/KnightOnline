// Phase 0 renderer spike: one deterministic fixed-function scene, described with
// D3D9's own enum values so the D3D9 backend can pass them straight through and the
// Metal backend has to emulate them. The state set comes from the client census
// (port/census): MODULATE/MODULATE2X/SELECTARG1/ADD over TEXTURE/DIFFUSE/CURRENT/
// TFACTOR, up to three stages, EXP2 table fog, alpha test GREATER 0, the engine's
// global -1 mip LOD bias, specular on, DXT1/3/5 + A4R4G4B4 + X8R8G8B8 + A8R8G8B8
// textures, the client's FVFs, points, lines, scissor, pre-transformed UI.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace ff
{

// --- D3D9 values (checked against d3d9types.h by the D3D9 backend) ---------------

enum : uint32_t
{
	TOP_DISABLE = 1, TOP_SELECTARG1 = 2, TOP_SELECTARG2 = 3, TOP_MODULATE = 4, TOP_MODULATE2X = 5,
	TOP_MODULATE4X = 6, TOP_ADD = 7,

	TA_DIFFUSE = 0, TA_CURRENT = 1, TA_TEXTURE = 2, TA_TFACTOR = 3, TA_SPECULAR = 4,
	TA_COMPLEMENT = 0x10, TA_ALPHAREPLICATE = 0x20,

	BLEND_ZERO = 1, BLEND_ONE = 2, BLEND_SRCCOLOR = 3, BLEND_INVSRCCOLOR = 4, BLEND_SRCALPHA = 5,
	BLEND_INVSRCALPHA = 6,

	CMP_ALWAYS = 8, CMP_GREATER = 5, CMP_LESSEQUAL = 4,

	CULL_NONE = 1, CULL_CW = 2, CULL_CCW = 3,

	TEXF_NONE = 0, TEXF_POINT = 1, TEXF_LINEAR = 2,

	TADDRESS_WRAP = 1, TADDRESS_MIRROR = 2, TADDRESS_CLAMP = 3,

	PT_POINTLIST = 1, PT_LINELIST = 2, PT_LINESTRIP = 3, PT_TRIANGLELIST = 4, PT_TRIANGLESTRIP = 5,
	PT_TRIANGLEFAN = 6,

	FVF_XYZ = 0x002, FVF_XYZRHW = 0x004, FVF_NORMAL = 0x010, FVF_DIFFUSE = 0x040, FVF_TEX1 = 0x100,
	FVF_TEX2 = 0x200,

	FOG_NONE = 0, FOG_EXP = 1, FOG_EXP2 = 2, FOG_LINEAR = 3,

	LIGHT_POINT = 1, LIGHT_DIRECTIONAL = 3,

	FMT_A8R8G8B8 = 21, FMT_X8R8G8B8 = 22, FMT_A4R4G4B4 = 26,
	FMT_DXT1 = 0x31545844, FMT_DXT3 = 0x33545844, FMT_DXT5 = 0x35545844,
};

// The client's vertex formats (N3Base/My_3DStruct.h)
constexpr uint32_t FVF_VNT1             = FVF_XYZ | FVF_NORMAL | FVF_TEX1;
constexpr uint32_t FVF_VNT2             = FVF_XYZ | FVF_NORMAL | FVF_TEX2;
constexpr uint32_t FVF_XYZCOLOR         = FVF_XYZ | FVF_DIFFUSE;
constexpr uint32_t FVF_XYZCOLORT1       = FVF_XYZ | FVF_DIFFUSE | FVF_TEX1;
constexpr uint32_t FVF_TRANSFORMED      = FVF_XYZRHW | FVF_DIFFUSE | FVF_TEX1;
constexpr uint32_t FVF_TRANSFORMEDCOLOR = FVF_XYZRHW | FVF_DIFFUSE;

inline uint32_t FvfStride(uint32_t fvf)
{
	uint32_t s = (fvf & FVF_XYZRHW) ? 16 : 12;
	if (fvf & FVF_NORMAL)
		s += 12;
	if (fvf & FVF_DIFFUSE)
		s += 4;
	if (fvf & FVF_TEX2)
		s += 16;
	else if (fvf & FVF_TEX1)
		s += 8;
	return s;
}

// --- math (row vectors, D3DX conventions) -------------------------------------

struct Mat4
{
	float m[4][4];
};

inline Mat4 Identity()
{
	Mat4 r {};
	for (int i = 0; i < 4; i++)
		r.m[i][i] = 1.0f;
	return r;
}

inline Mat4 Mul(const Mat4& a, const Mat4& b)
{
	Mat4 r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				r.m[i][j] += a.m[i][k] * b.m[k][j];
	return r;
}

inline Mat4 Translation(float x, float y, float z)
{
	Mat4 r    = Identity();
	r.m[3][0] = x;
	r.m[3][1] = y;
	r.m[3][2] = z;
	return r;
}

inline Mat4 Scaling(float x, float y, float z)
{
	Mat4 r    = Identity();
	r.m[0][0] = x;
	r.m[1][1] = y;
	r.m[2][2] = z;
	return r;
}

inline Mat4 RotationY(float a)
{
	Mat4 r    = Identity();
	r.m[0][0] = std::cos(a);
	r.m[0][2] = -std::sin(a);
	r.m[2][0] = std::sin(a);
	r.m[2][2] = std::cos(a);
	return r;
}

// D3DXMatrixPerspectiveFovLH
inline Mat4 PerspectiveFovLH(float fovy, float aspect, float zn, float zf)
{
	float ys = 1.0f / std::tan(fovy / 2.0f);
	float xs = ys / aspect;
	Mat4 r {};
	r.m[0][0] = xs;
	r.m[1][1] = ys;
	r.m[2][2] = zf / (zf - zn);
	r.m[2][3] = 1.0f;
	r.m[3][2] = -zn * zf / (zf - zn);
	return r;
}

struct V3
{
	float x, y, z;
};

inline V3 Sub(V3 a, V3 b)
{
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline V3 Cross(V3 a, V3 b)
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline float Dot(V3 a, V3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline V3 Normalize(V3 a)
{
	float l = std::sqrt(Dot(a, a));
	return { a.x / l, a.y / l, a.z / l };
}

// D3DXMatrixLookAtLH
inline Mat4 LookAtLH(V3 eye, V3 at, V3 up)
{
	V3 z   = Normalize(Sub(at, eye));
	V3 x   = Normalize(Cross(up, z));
	V3 y   = Cross(z, x);
	Mat4 r = Identity();
	r.m[0][0] = x.x;
	r.m[1][0] = x.y;
	r.m[2][0] = x.z;
	r.m[0][1] = y.x;
	r.m[1][1] = y.y;
	r.m[2][1] = y.z;
	r.m[0][2] = z.x;
	r.m[1][2] = z.y;
	r.m[2][2] = z.z;
	r.m[3][0] = -Dot(x, eye);
	r.m[3][1] = -Dot(y, eye);
	r.m[3][2] = -Dot(z, eye);
	return r;
}

// --- scene description ----------------------------------------------------------

// Same layout as D3DCOLORVALUE / D3DMATERIAL9 / D3DLIGHT9 (static_asserted on Windows).
struct Color4
{
	float r, g, b, a;
};

struct Material
{
	Color4 diffuse, ambient, specular, emissive;
	float power;
};

struct Light
{
	uint32_t type;
	Color4 diffuse, specular, ambient;
	V3 position, direction;
	float range, falloff, attenuation0, attenuation1, attenuation2, theta, phi;
};

struct Texture
{
	uint32_t format;
	int width, height;
	std::vector<std::vector<uint8_t>> levels; // D3D memory layout, tightly packed rows
};

struct Stage
{
	uint32_t colorOp   = TOP_DISABLE, colorArg1 = TA_TEXTURE, colorArg2 = TA_CURRENT;
	uint32_t alphaOp   = TOP_DISABLE, alphaArg1 = TA_TEXTURE, alphaArg2 = TA_CURRENT;
	int texture        = -1;
	uint32_t texCoordIndex = 0;
	uint32_t minFilter = TEXF_LINEAR, magFilter = TEXF_LINEAR, mipFilter = TEXF_LINEAR;
	uint32_t addressU = TADDRESS_WRAP, addressV = TADDRESS_WRAP;
};

struct Draw
{
	const char* name;
	uint32_t fvf, primType, primCount;
	std::vector<uint8_t> vertices;
	std::vector<uint16_t> indices;
	Mat4 world           = Identity();
	bool lighting        = false;
	bool specular        = true; // engine default: D3DRS_SPECULARENABLE TRUE
	bool zEnable         = true, zWrite = true;
	bool alphaBlend      = false;
	bool alphaTest       = true; // engine default: TRUE, GREATER, ref 0
	uint32_t alphaRef    = 0;
	bool fog             = true;
	bool scissor         = false;
	int scissorRect[4]   = { 0, 0, 0, 0 }; // left, top, right, bottom
	uint32_t srcBlend    = BLEND_SRCALPHA, destBlend = BLEND_INVSRCALPHA;
	uint32_t cullMode    = CULL_CCW;
	uint32_t textureFactor = 0xFFFFFFFF;
	Material material    = { { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0 };
	uint32_t lightMask   = 0;
	Stage stages[3];
};

struct Scene
{
	int width = 320, height = 240;
	uint32_t clearColor = 0xFF000000;
	Mat4 view, proj;
	uint32_t ambient;
	uint32_t fogColor;
	float fogDensity;
	float mipLodBias = -1.0f; // N3Eng sets -1 on every sampler
	Light lights[2];
	std::vector<Texture> textures;
	std::vector<Draw> draws;
};

// --- procedural textures --------------------------------------------------------

inline uint16_t To565(int r, int g, int b)
{
	return (uint16_t) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline void From565(uint16_t c, int& r, int& g, int& b)
{
	r = ((c >> 11) & 31) * 255 / 31;
	g = ((c >> 5) & 63) * 255 / 63;
	b = (c & 31) * 255 / 31;
}

// RGBA8 image, row-major
struct Image
{
	int w, h;
	std::vector<uint8_t> px; // r,g,b,a

	uint8_t* at(int x, int y)
	{
		return &px[(size_t) (y * w + x) * 4];
	}
};

inline Image Downsample(Image& s)
{
	Image d { s.w > 1 ? s.w / 2 : 1, s.h > 1 ? s.h / 2 : 1, {} };
	d.px.resize((size_t) d.w * d.h * 4);
	for (int y = 0; y < d.h; y++)
		for (int x = 0; x < d.w; x++)
			for (int c = 0; c < 4; c++)
			{
				int sx = x * 2, sy = y * 2;
				int sx1 = std::min(sx + 1, s.w - 1), sy1 = std::min(sy + 1, s.h - 1);
				int v   = s.at(sx, sy)[c] + s.at(sx1, sy)[c] + s.at(sx, sy1)[c] + s.at(sx1, sy1)[c];
				d.at(x, y)[c] = (uint8_t) ((v + 2) / 4);
			}
	return d;
}

// Minimal deterministic BC encoders: block quality is irrelevant, both backends just
// have to decode the same bytes.
inline void EncodeColorBlock(Image& img, int bx, int by, bool punchThrough, uint8_t* out)
{
	int minL = 1 << 30, maxL = -1;
	int lo[3] = {}, hi[3] = {};
	bool anyTransparent = false;
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
		{
			uint8_t* p = img.at(std::min(bx * 4 + x, img.w - 1), std::min(by * 4 + y, img.h - 1));
			if (punchThrough && p[3] < 128)
			{
				anyTransparent = true;
				continue;
			}
			int l = p[0] * 3 + p[1] * 6 + p[2];
			if (l < minL)
			{
				minL = l;
				lo[0] = p[0], lo[1] = p[1], lo[2] = p[2];
			}
			if (l > maxL)
			{
				maxL = l;
				hi[0] = p[0], hi[1] = p[1], hi[2] = p[2];
			}
		}
	uint16_t c0 = To565(hi[0], hi[1], hi[2]), c1 = To565(lo[0], lo[1], lo[2]);
	bool threeColor = punchThrough && anyTransparent;
	if (threeColor ? c0 > c1 : c0 < c1)
		std::swap(c0, c1);
	if (!threeColor && c0 == c1)
	{
		// keep 4-colour mode (c0 > c1) even for flat blocks
		if (c0 > 0)
			c1 = (uint16_t) (c0 - 1);
		else
			c0 = 1;
	}
	int pal[4][3];
	From565(c0, pal[0][0], pal[0][1], pal[0][2]);
	From565(c1, pal[1][0], pal[1][1], pal[1][2]);
	for (int c = 0; c < 3; c++)
	{
		if (threeColor)
		{
			pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
			pal[3][c] = 0;
		}
		else
		{
			pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
			pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
		}
	}
	uint32_t bits = 0;
	for (int i = 0; i < 16; i++)
	{
		uint8_t* p = img.at(std::min(bx * 4 + i % 4, img.w - 1), std::min(by * 4 + i / 4, img.h - 1));
		int best = 0, bestD = 1 << 30;
		if (threeColor && p[3] < 128)
			best = 3;
		else
			for (int k = 0; k < (threeColor ? 3 : 4); k++)
			{
				int d = 0;
				for (int c = 0; c < 3; c++)
					d += (p[c] - pal[k][c]) * (p[c] - pal[k][c]);
				if (d < bestD)
					bestD = d, best = k;
			}
		bits |= (uint32_t) best << (2 * i);
	}
	memcpy(out, &c0, 2);
	memcpy(out + 2, &c1, 2);
	memcpy(out + 4, &bits, 4);
}

inline std::vector<uint8_t> EncodeBC(Image& img, uint32_t fmt)
{
	int bw = (img.w + 3) / 4, bh = (img.h + 3) / 4;
	size_t blockSize = fmt == FMT_DXT1 ? 8 : 16;
	std::vector<uint8_t> out((size_t) bw * bh * blockSize);
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++)
		{
			uint8_t* b = &out[((size_t) by * bw + bx) * blockSize];
			if (fmt == FMT_DXT3)
			{
				for (int i = 0; i < 16; i++)
				{
					uint8_t a = img.at(std::min(bx * 4 + i % 4, img.w - 1), std::min(by * 4 + i / 4, img.h - 1))[3];
					uint8_t nib = (uint8_t) ((a * 15 + 127) / 255);
					b[i / 2] |= (uint8_t) (nib << ((i & 1) * 4));
				}
				b += 8;
			}
			else if (fmt == FMT_DXT5)
			{
				int amin = 255, amax = 0;
				for (int i = 0; i < 16; i++)
				{
					int a = img.at(std::min(bx * 4 + i % 4, img.w - 1), std::min(by * 4 + i / 4, img.h - 1))[3];
					amin = std::min(amin, a), amax = std::max(amax, a);
				}
				if (amax == amin)
					amax = std::min(255, amin + 1), amin = amax - 1;
				b[0] = (uint8_t) amax, b[1] = (uint8_t) amin; // a0 > a1: 8-value mode
				uint64_t bits = 0;
				for (int i = 0; i < 16; i++)
				{
					int a = img.at(std::min(bx * 4 + i % 4, img.w - 1), std::min(by * 4 + i / 4, img.h - 1))[3];
					// palette: 0 = a0, 1 = a1, 2..7 = interpolated from a0 towards a1
					int t     = (amax - a) * 7 / (amax - amin); // 0 (=a0) .. 7 (=a1)
					int code  = t == 0 ? 0 : (t == 7 ? 1 : t + 1);
					bits |= (uint64_t) code << (3 * i);
				}
				for (int k = 0; k < 6; k++)
					b[2 + k] = (uint8_t) (bits >> (8 * k));
				b += 8;
			}
			EncodeColorBlock(img, bx, by, fmt == FMT_DXT1, b);
		}
	return out;
}

inline Texture MakeTexture(Image base, uint32_t fmt, int levels)
{
	Texture t { fmt, base.w, base.h, {} };
	Image cur = base;
	for (int l = 0; l < levels; l++)
	{
		std::vector<uint8_t> data;
		if (fmt == FMT_DXT1 || fmt == FMT_DXT3 || fmt == FMT_DXT5)
			data = EncodeBC(cur, fmt);
		else if (fmt == FMT_A4R4G4B4)
		{
			for (size_t i = 0; i < cur.px.size(); i += 4)
			{
				uint16_t v = (uint16_t) (((cur.px[i + 3] >> 4) << 12) | ((cur.px[i] >> 4) << 8)
										 | ((cur.px[i + 1] >> 4) << 4) | (cur.px[i + 2] >> 4));
				data.push_back((uint8_t) v);
				data.push_back((uint8_t) (v >> 8));
			}
		}
		else // A8R8G8B8 / X8R8G8B8: bytes B, G, R, A
		{
			for (size_t i = 0; i < cur.px.size(); i += 4)
			{
				data.push_back(cur.px[i + 2]);
				data.push_back(cur.px[i + 1]);
				data.push_back(cur.px[i]);
				data.push_back(fmt == FMT_X8R8G8B8 ? 0xFF : cur.px[i + 3]);
			}
		}
		t.levels.push_back(std::move(data));
		if (cur.w == 1 && cur.h == 1)
			break;
		cur = Downsample(cur);
	}
	return t;
}

inline Image NewImage(int w, int h)
{
	return Image { w, h, std::vector<uint8_t>((size_t) w * h * 4, 0) };
}

inline void Put(Image& img, int x, int y, int r, int g, int b, int a)
{
	uint8_t* p = img.at(x, y);
	p[0] = (uint8_t) r, p[1] = (uint8_t) g, p[2] = (uint8_t) b, p[3] = (uint8_t) a;
}

// --- geometry helpers -----------------------------------------------------------

struct VB
{
	std::vector<uint8_t> bytes;

	template <typename T>
	void put(T v)
	{
		const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
		bytes.insert(bytes.end(), p, p + sizeof(T));
	}

	void f(float v)
	{
		put(v);
	}

	void c(uint32_t v)
	{
		put(v);
	}
};

inline uint32_t ARGB(int a, int r, int g, int b)
{
	return ((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}

// --- the scene ------------------------------------------------------------------

inline float TerrainHeight(float x, float z)
{
	return 0.6f * std::sin(x * 0.5f) * std::cos(z * 0.4f) + 0.3f * std::sin(z * 1.1f + 0.5f);
}

inline Scene BuildScene()
{
	Scene s;
	s.clearColor = ARGB(255, 169, 203, 215); // noon fog colour (N3SkyMng)
	s.fogColor   = s.clearColor;
	float farP   = 48.0f;
	s.fogDensity = 1.0f / (0.37f * farP); // CN3Camera::Apply
	s.view       = LookAtLH({ 0.0f, 3.2f, -9.0f }, { 0.0f, 0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f });
	s.proj       = PerspectiveFovLH(1.0471976f, 4.0f / 3.0f, 0.5f, farP);
	s.ambient    = ARGB(0, 48, 48, 64);

	Light sun {};
	sun.type      = LIGHT_DIRECTIONAL;
	sun.diffuse   = { 0.9f, 0.85f, 0.7f, 1.0f };
	sun.specular  = { 0.8f, 0.8f, 0.8f, 1.0f };
	sun.ambient   = { 0.15f, 0.15f, 0.2f, 1.0f };
	sun.direction = Normalize({ 0.4f, -0.8f, 0.45f });
	sun.range     = 1e6f;
	s.lights[0]   = sun;

	Light lamp {};
	lamp.type         = LIGHT_POINT;
	lamp.diffuse      = { 1.0f, 0.55f, 0.2f, 1.0f };
	lamp.specular     = { 1.0f, 0.8f, 0.6f, 1.0f };
	lamp.ambient      = { 0.0f, 0.0f, 0.0f, 1.0f };
	lamp.position     = { -1.5f, 2.0f, -1.5f };
	lamp.range        = 9.0f;
	lamp.attenuation0 = 0.6f;
	lamp.attenuation1 = 0.15f;
	lamp.attenuation2 = 0.05f;
	s.lights[1]       = lamp;

	// 0: terrain tile (DXT1, full mip chain)
	{
		Image img = NewImage(64, 64);
		for (int y = 0; y < 64; y++)
			for (int x = 0; x < 64; x++)
			{
				bool chk = ((x / 8) + (y / 8)) & 1;
				int g    = 110 + (x * 2) % 60;
				Put(img, x, y, chk ? 90 : 60, chk ? g : g - 30, chk ? 50 : 40, 255);
			}
		s.textures.push_back(MakeTexture(img, FMT_DXT1, 7));
	}
	// 1: lightmap (X8R8G8B8, one level; alpha byte deliberately garbage-free 0xFF)
	{
		Image img = NewImage(32, 32);
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				float d = std::sqrt((float) ((x - 10) * (x - 10) + (y - 20) * (y - 20))) / 32.0f;
				int v   = (int) (255.0f * std::max(0.35f, 1.0f - d));
				Put(img, x, y, v, v, std::min(255, v + 20), 255);
			}
		s.textures.push_back(MakeTexture(img, FMT_X8R8G8B8, 1));
	}
	// 2: foliage cut-out (DXT1 with 1-bit alpha)
	{
		Image img = NewImage(64, 64);
		for (int y = 0; y < 64; y++)
			for (int x = 0; x < 64; x++)
			{
				float fx = (x - 32) / 32.0f, fy = (y - 40) / 40.0f;
				bool in  = (fx * fx * 2.5f + fy * fy) < 0.9f && ((x + y / 3) % 7 != 0);
				Put(img, x, y, 40 + y, 120 + x % 50, 30, in ? 255 : 0);
			}
		s.textures.push_back(MakeTexture(img, FMT_DXT1, 1));
	}
	// 3: object (DXT3, 4 levels)
	{
		Image img = NewImage(64, 64);
		for (int y = 0; y < 64; y++)
			for (int x = 0; x < 64; x++)
			{
				int band = (y / 6) & 1;
				Put(img, x, y, band ? 170 : 120, band ? 120 : 90, band ? 80 : 60, ((x / 16) & 1) ? 255 : 160);
			}
		s.textures.push_back(MakeTexture(img, FMT_DXT3, 4));
	}
	// 4: fx glow (DXT5, radial alpha)
	{
		Image img = NewImage(32, 32);
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				float d = std::sqrt((float) ((x - 15.5f) * (x - 15.5f) + (y - 15.5f) * (y - 15.5f))) / 16.0f;
				int a   = (int) (255.0f * std::max(0.0f, 1.0f - d));
				Put(img, x, y, 255, 200, 90, a);
			}
		s.textures.push_back(MakeTexture(img, FMT_DXT5, 1));
	}
	// 5: UI panel (A4R4G4B4)
	{
		Image img = NewImage(32, 32);
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				bool edge = x < 2 || y < 2 || x > 29 || y > 29;
				Put(img, x, y, edge ? 240 : 40, edge ? 200 : 50, edge ? 120 : 80, edge ? 255 : 192);
			}
		s.textures.push_back(MakeTexture(img, FMT_A4R4G4B4, 1));
	}
	// 6: pixel-art icon (A8R8G8B8), magnified with point sampling
	{
		Image img = NewImage(16, 16);
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++)
			{
				bool ring = std::abs((x - 7.5f) * (x - 7.5f) + (y - 7.5f) * (y - 7.5f) - 30.0f) < 12.0f;
				Put(img, x, y, ring ? 250 : 30 + x * 10, ring ? 230 : 30, ring ? 60 : 30 + y * 12, ring ? 255 : 128);
			}
		s.textures.push_back(MakeTexture(img, FMT_A8R8G8B8, 1));
	}

	// Sky gradient (N3Sky: FVF_XYZCOLOR fan, no light/fog/z)
	{
		Draw d {};
		d.name = "sky";
		d.fvf  = FVF_XYZCOLOR;
		VB vb;
		auto v = [&](float x, float y, float z, uint32_t c) {
			vb.f(x), vb.f(y), vb.f(z), vb.c(c);
		};
		v(-60, 30, 40, ARGB(255, 70, 110, 180));
		v(60, 30, 40, ARGB(255, 70, 110, 180));
		v(60, -2, 40, ARGB(255, 169, 203, 215));
		v(-60, -2, 40, ARGB(255, 169, 203, 215));
		d.vertices  = vb.bytes;
		d.primType  = PT_TRIANGLEFAN;
		d.primCount = 2;
		d.zEnable = false, d.zWrite = false, d.fog = false, d.cullMode = CULL_NONE;
		d.stages[0] = { TOP_SELECTARG1, TA_DIFFUSE, TA_CURRENT, TOP_SELECTARG1, TA_DIFFUSE, TA_CURRENT };
		s.draws.push_back(d);
	}
	// Stars (N3Star: point list)
	{
		Draw d {};
		d.name = "stars";
		d.fvf  = FVF_XYZCOLOR;
		VB vb;
		for (int i = 0; i < 48; i++)
		{
			float x = -40.0f + (float) ((i * 37) % 80), y = 8.0f + (float) ((i * 53) % 20);
			vb.f(x), vb.f(y), vb.f(40.0f), vb.c(ARGB(255, 255, 255, 200 + (i % 5) * 10));
		}
		d.vertices  = vb.bytes;
		d.primType  = PT_POINTLIST;
		d.primCount = 48;
		d.zEnable = false, d.zWrite = false, d.fog = false;
		d.stages[0] = { TOP_SELECTARG1, TA_DIFFUSE, TA_CURRENT, TOP_SELECTARG1, TA_DIFFUSE, TA_CURRENT };
		s.draws.push_back(d);
	}
	// Terrain (N3TerrainPatch-like: 3 stages, VNT2, lit, fogged)
	{
		Draw d {};
		d.name     = "terrain";
		d.fvf      = FVF_VNT2;
		const int N = 16;
		const float size = 24.0f, x0 = -12.0f, z0 = -6.0f;
		VB vb;
		for (int j = 0; j <= N; j++)
			for (int i = 0; i <= N; i++)
			{
				float x = x0 + size * i / N, z = z0 + size * j / N, y = TerrainHeight(x, z);
				float e = 0.01f;
				V3 n    = Normalize({ -(TerrainHeight(x + e, z) - TerrainHeight(x - e, z)) / (2 * e), 1.0f,
					   -(TerrainHeight(x, z + e) - TerrainHeight(x, z - e)) / (2 * e) });
				vb.f(x), vb.f(y), vb.f(z), vb.f(n.x), vb.f(n.y), vb.f(n.z);
				vb.f(8.0f * i / N), vb.f(8.0f * j / N); // tile UV
				vb.f(2.0f * i / N), vb.f(2.0f * j / N); // lightmap UV, mirrored
			}
		for (int j = 0; j < N; j++)
			for (int i = 0; i < N; i++)
			{
				uint16_t a = (uint16_t) (j * (N + 1) + i), b = (uint16_t) (a + 1), c = (uint16_t) (a + N + 1), e = (uint16_t) (c + 1);
				// clockwise when viewed from above (front faces in D3D's left-handed default)
				d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
			}
		d.vertices  = vb.bytes;
		d.primType  = PT_TRIANGLELIST;
		d.primCount = N * N * 2;
		d.lighting  = true;
		d.lightMask = 1;
		d.stages[0] = { TOP_SELECTARG1, TA_TEXTURE, TA_DIFFUSE, TOP_SELECTARG1, TA_TEXTURE, TA_CURRENT, 0, 0 };
		d.stages[1] = { TOP_MODULATE, TA_TEXTURE, TA_CURRENT, TOP_DISABLE, TA_TEXTURE, TA_CURRENT, 1, 1 };
		d.stages[1].addressU = d.stages[1].addressV = TADDRESS_MIRROR;
		d.stages[1].mipFilter = TEXF_NONE;
		d.stages[2] = { TOP_MODULATE, TA_CURRENT, TA_DIFFUSE, TOP_DISABLE, TA_TEXTURE, TA_CURRENT, -1, 0 };
		s.draws.push_back(d);
	}
	// Object: lit box, MODULATE2X (asset materials use it), point + directional light, specular
	{
		Draw d {};
		d.name = "object";
		d.fvf  = FVF_VNT1;
		VB vb;
		const V3 faces[6][4] = {
			{ { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 }, { 1, -1, -1 } },
			{ { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }, { -1, -1, 1 } },
			{ { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, -1 } },
			{ { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { 1, -1, 1 } },
			{ { -1, 1, -1 }, { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 } },
			{ { -1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 } },
		};
		const float uv[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
		for (int f = 0; f < 6; f++)
		{
			V3 n = Normalize(Cross(Sub(faces[f][1], faces[f][0]), Sub(faces[f][2], faces[f][0])));
			for (int k = 0; k < 4; k++)
				vb.f(faces[f][k].x), vb.f(faces[f][k].y), vb.f(faces[f][k].z), vb.f(n.x), vb.f(n.y), vb.f(n.z),
					vb.f(uv[k][0]), vb.f(uv[k][1]);
			uint16_t b = (uint16_t) (f * 4);
			d.indices.insert(d.indices.end(), { b, (uint16_t) (b + 1), (uint16_t) (b + 2), b, (uint16_t) (b + 2), (uint16_t) (b + 3) });
		}
		d.vertices  = vb.bytes;
		d.primType  = PT_TRIANGLELIST;
		d.primCount = 12;
		d.world     = Mul(Mul(Scaling(0.9f, 0.9f, 0.9f), RotationY(0.6f)), Translation(-2.2f, 1.3f, 0.5f));
		d.lighting  = true;
		d.lightMask = 3;
		d.material  = { { 0.8f, 0.75f, 0.7f, 1.0f }, { 0.6f, 0.6f, 0.6f, 1.0f }, { 0.5f, 0.5f, 0.5f, 1.0f }, { 0.05f, 0.0f, 0.0f, 0.0f }, 12.0f };
		d.stages[0] = { TOP_MODULATE2X, TA_DIFFUSE, TA_TEXTURE, TOP_SELECTARG1, TA_TEXTURE, TA_CURRENT, 3, 0 };
		s.draws.push_back(d);
	}
	// Foliage: crossed double-sided quads, alpha test + blend, DXT1 punch-through
	{
		Draw d {};
		d.name = "foliage";
		d.fvf  = FVF_VNT1;
		VB vb;
		auto quad = [&](float ax, float az, float bx, float bz) {
			V3 n = Normalize(Cross({ bx - ax, 0, bz - az }, { 0, 1, 0 }));
			float h = 2.6f;
			float p[4][3] = { { ax, 0.2f, az }, { ax, 0.2f + h, az }, { bx, 0.2f + h, bz }, { bx, 0.2f, bz } };
			float t[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
			for (int k = 0; k < 4; k++)
				vb.f(p[k][0] + 2.4f), vb.f(p[k][1]), vb.f(p[k][2]), vb.f(n.x), vb.f(n.y), vb.f(n.z), vb.f(t[k][0]), vb.f(t[k][1]);
		};
		quad(-1.2f, -0.3f, 1.2f, 0.3f);
		quad(-0.3f, -1.2f, 0.3f, 1.2f);
		d.indices   = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
		d.vertices  = vb.bytes;
		d.primType  = PT_TRIANGLELIST;
		d.primCount = 4;
		d.lighting  = true;
		d.lightMask = 1;
		d.alphaBlend = true;
		d.cullMode   = CULL_NONE;
		d.stages[0]  = { TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, 2, 0 };
		s.draws.push_back(d);
	}
	// Unbound texture: MODULATE(TEXTURE, DIFFUSE) with no texture set (what does D3D9 return?)
	{
		Draw d {};
		d.name = "unbound-texture";
		d.fvf  = FVF_XYZCOLOR;
		VB vb;
		vb.f(0.4f), vb.f(0.2f), vb.f(-2.5f), vb.c(ARGB(255, 255, 64, 64));
		vb.f(0.4f), vb.f(1.0f), vb.f(-2.5f), vb.c(ARGB(255, 64, 255, 64));
		vb.f(1.2f), vb.f(1.0f), vb.f(-2.5f), vb.c(ARGB(255, 64, 64, 255));
		vb.f(1.2f), vb.f(0.2f), vb.f(-2.5f), vb.c(ARGB(255, 255, 255, 64));
		d.vertices  = vb.bytes;
		d.primType  = PT_TRIANGLEFAN;
		d.primCount = 2;
		d.cullMode  = CULL_NONE;
		d.stages[0] = { TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, -1, 0 };
		s.draws.push_back(d);
	}
	// TFACTOR (PlayerBase): MODULATE(TEXTURE, TFACTOR), alpha from TFACTOR, blended
	{
		Draw d {};
		d.name = "tfactor";
		d.fvf  = FVF_XYZCOLORT1;
		VB vb;
		float q[4][5] = { { -0.6f, 0.3f, -3.0f, 0, 1 }, { -0.6f, 1.1f, -3.0f, 0, 0 }, { 0.2f, 1.1f, -3.0f, 1, 0 }, { 0.2f, 0.3f, -3.0f, 1, 1 } };
		for (auto& p : q)
			vb.f(p[0]), vb.f(p[1]), vb.f(p[2]), vb.c(0xFFFFFFFF), vb.f(p[3]), vb.f(p[4]);
		d.vertices      = vb.bytes;
		d.primType      = PT_TRIANGLEFAN;
		d.primCount     = 2;
		d.alphaBlend    = true;
		d.cullMode      = CULL_NONE;
		d.textureFactor = ARGB(160, 255, 128, 64);
		d.stages[0]     = { TOP_MODULATE, TA_TEXTURE, TA_TFACTOR, TOP_SELECTARG1, TA_TFACTOR, TA_CURRENT, 3, 0 };
		s.draws.push_back(d);
	}
	// FX glow billboard: additive ONE/ONE, no z-write, no fog (RF_NOTUSEFOG)
	{
		Draw d {};
		d.name = "fx";
		d.fvf  = FVF_XYZCOLORT1;
		VB vb;
		float q[4][5] = { { 1.0f, 1.2f, -1.0f, 0, 1 }, { 1.0f, 3.2f, -1.0f, 0, 0 }, { 3.0f, 3.2f, -1.0f, 1, 0 }, { 3.0f, 1.2f, -1.0f, 1, 1 } };
		for (auto& p : q)
			vb.f(p[0]), vb.f(p[1]), vb.f(p[2]), vb.c(ARGB(200, 255, 255, 255)), vb.f(p[3]), vb.f(p[4]);
		d.vertices   = vb.bytes;
		d.primType   = PT_TRIANGLEFAN;
		d.primCount  = 2;
		d.alphaBlend = true;
		d.srcBlend = BLEND_ONE, d.destBlend = BLEND_ONE;
		d.zWrite    = false;
		d.fog       = false;
		d.cullMode  = CULL_NONE;
		d.stages[0] = { TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, 4, 0 };
		s.draws.push_back(d);
	}
	// UI panel: pre-transformed, A4R4G4B4, point sampling, no mips (UIManager state)
	{
		Draw d {};
		d.name = "ui-panel";
		d.fvf  = FVF_TRANSFORMED;
		VB vb;
		float q[4][4] = { { 8, 168, 0, 1 }, { 8, 232, 0, 0 }, { 120, 232, 1, 0 }, { 120, 168, 1, 1 } };
		for (auto& p : q)
			vb.f(p[0]), vb.f(p[1]), vb.f(0.0f), vb.f(1.0f), vb.c(ARGB(230, 255, 255, 255)), vb.f(p[2]), vb.f(p[3]);
		d.vertices   = vb.bytes;
		d.primType   = PT_TRIANGLEFAN;
		d.primCount  = 2;
		d.alphaBlend = true;
		d.zEnable = false, d.zWrite = false, d.fog = false, d.cullMode = CULL_NONE;
		d.stages[0] = { TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, 5, 0 };
		d.stages[0].minFilter = d.stages[0].magFilter = TEXF_POINT;
		d.stages[0].mipFilter = TEXF_NONE;
		s.draws.push_back(d);
	}
	// UI icon: 16x16 magnified 4x with point sampling, clipped by a scissor rect
	{
		Draw d {};
		d.name = "ui-icon-scissor";
		d.fvf  = FVF_TRANSFORMED;
		VB vb;
		float q[4][4] = { { 248, 8, 0, 0 }, { 312, 8, 1, 0 }, { 312, 72, 1, 1 }, { 248, 72, 0, 1 } };
		for (auto& p : q)
			vb.f(p[0]), vb.f(p[1]), vb.f(0.0f), vb.f(1.0f), vb.c(0xFFFFFFFF), vb.f(p[2]), vb.f(p[3]);
		d.vertices   = vb.bytes;
		d.primType   = PT_TRIANGLEFAN;
		d.primCount  = 2;
		d.alphaBlend = true;
		d.zEnable = false, d.zWrite = false, d.fog = false, d.cullMode = CULL_NONE;
		d.scissor        = true;
		d.scissorRect[0] = 248, d.scissorRect[1] = 8, d.scissorRect[2] = 300, d.scissorRect[3] = 60;
		d.stages[0] = { TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, TOP_MODULATE, TA_TEXTURE, TA_DIFFUSE, 6, 0 };
		d.stages[0].minFilter = d.stages[0].magFilter = TEXF_POINT;
		d.stages[0].mipFilter = TEXF_NONE;
		s.draws.push_back(d);
	}
	// UI border: pre-transformed line strip (FVF_TRANSFORMEDCOLOR, N3UIBase/N3UIList)
	{
		Draw d {};
		d.name = "ui-lines";
		d.fvf  = FVF_TRANSFORMEDCOLOR;
		VB vb;
		float p[5][2] = { { 6, 166 }, { 122, 166 }, { 122, 234 }, { 6, 234 }, { 6, 166 } };
		for (auto& q : p)
			vb.f(q[0]), vb.f(q[1]), vb.f(0.0f), vb.f(1.0f), vb.c(ARGB(255, 255, 220, 120));
		d.vertices  = vb.bytes;
		d.primType  = PT_LINESTRIP;
		d.primCount = 4;
		d.zEnable = false, d.zWrite = false, d.fog = false;
		d.stages[0] = { TOP_SELECTARG1, TA_DIFFUSE, TA_CURRENT, TOP_SELECTARG1, TA_DIFFUSE, TA_CURRENT };
		s.draws.push_back(d);
	}
	return s;
}

inline uint32_t VertexCount(const Draw& d)
{
	return (uint32_t) (d.vertices.size() / FvfStride(d.fvf));
}

} // namespace ff

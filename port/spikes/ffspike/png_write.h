// Dependency-free PNG writer (stored deflate blocks) for the renderer spike.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace ff
{

inline uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0)
{
	crc = ~crc;
	for (size_t i = 0; i < n; i++)
	{
		crc ^= p[i];
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

// rgba: width*height*4 bytes, row-major, top row first
inline bool WritePng(const char* path, int width, int height, const uint8_t* rgba)
{
	std::vector<uint8_t> raw;
	raw.reserve((size_t) height * (width * 4 + 1));
	for (int y = 0; y < height; y++)
	{
		raw.push_back(0); // filter: none
		raw.insert(raw.end(), rgba + (size_t) y * width * 4, rgba + (size_t) (y + 1) * width * 4);
	}

	std::vector<uint8_t> z = { 0x78, 0x01 };
	uint32_t a = 1, b = 0;
	for (uint8_t v : raw)
	{
		a = (a + v) % 65521;
		b = (b + a) % 65521;
	}
	for (size_t off = 0; off < raw.size() || off == 0;)
	{
		size_t n   = raw.size() - off < 65535 ? raw.size() - off : 65535;
		bool last  = off + n == raw.size();
		z.push_back(last ? 1 : 0);
		z.push_back((uint8_t) n);
		z.push_back((uint8_t) (n >> 8));
		z.push_back((uint8_t) ~n);
		z.push_back((uint8_t) (~n >> 8));
		z.insert(z.end(), raw.begin() + (long) off, raw.begin() + (long) (off + n));
		off += n;
		if (last)
			break;
	}
	uint32_t adler = (b << 16) | a;
	for (int i = 3; i >= 0; i--)
		z.push_back((uint8_t) (adler >> (8 * i)));

	FILE* f = std::fopen(path, "wb");
	if (f == nullptr)
		return false;
	auto be32 = [&](uint32_t v) {
		uint8_t c[4] = { (uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v };
		std::fwrite(c, 1, 4, f);
	};
	auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
		be32((uint32_t) data.size());
		std::vector<uint8_t> td(type, type + 4);
		td.insert(td.end(), data.begin(), data.end());
		std::fwrite(td.data(), 1, td.size(), f);
		be32(Crc32(td.data(), td.size()));
	};
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	std::fwrite(sig, 1, 8, f);
	std::vector<uint8_t> ihdr = { (uint8_t) (width >> 24), (uint8_t) (width >> 16), (uint8_t) (width >> 8), (uint8_t) width,
		(uint8_t) (height >> 24), (uint8_t) (height >> 16), (uint8_t) (height >> 8), (uint8_t) height, 8, 6, 0, 0, 0 };
	chunk("IHDR", ihdr);
	chunk("IDAT", z);
	chunk("IEND", {});
	std::fclose(f);
	return true;
}

} // namespace ff

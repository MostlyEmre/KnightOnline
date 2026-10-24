#include "../Crypto.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// CryptoAPI's RC4 decryption without CryptoAPI: SHA-1 and RC4 in portable C++.
// CryptDeriveKey(CALG_RC4, SHA-1 hash, 128-bit) keys RC4 with the first 16 bytes of the hash
// (no salt with the enhanced provider), and CryptDecrypt(Final = TRUE) restarts the keystream
// every call.

namespace Platform
{

namespace
{

constexpr size_t Rc4KeyLength = 16; // 0x800000 >> 16 = 128 bits

uint32_t RotateLeft(uint32_t value, int bits)
{
	return (value << bits) | (value >> (32 - bits));
}

std::array<uint8_t, 20> Sha1(std::string_view data)
{
	uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

	std::vector<uint8_t> message(data.begin(), data.end());
	const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
	message.push_back(0x80);
	while (message.size() % 64 != 56)
		message.push_back(0);
	for (int i = 7; i >= 0; --i)
		message.push_back(static_cast<uint8_t>(bitLength >> (i * 8)));

	for (size_t chunk = 0; chunk < message.size(); chunk += 64)
	{
		std::array<uint32_t, 80> w {};
		for (size_t i = 0; i < 16; ++i)
		{
			const size_t at = chunk + (i * 4);
			w[i]            = (uint32_t(message[at]) << 24) | (uint32_t(message[at + 1]) << 16)
				   | (uint32_t(message[at + 2]) << 8) | uint32_t(message[at + 3]);
		}

		for (size_t i = 16; i < 80; ++i)
			w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

		uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
		for (size_t i = 0; i < 80; ++i)
		{
			uint32_t f = 0, k = 0;
			if (i < 20)
			{
				f = (b & c) | (~b & d);
				k = 0x5A827999;
			}
			else if (i < 40)
			{
				f = b ^ c ^ d;
				k = 0x6ED9EBA1;
			}
			else if (i < 60)
			{
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDC;
			}
			else
			{
				f = b ^ c ^ d;
				k = 0xCA62C1D6;
			}

			uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
			e             = d;
			d             = c;
			c             = RotateLeft(b, 30);
			b             = a;
			a             = temp;
		}

		h0 += a;
		h1 += b;
		h2 += c;
		h3 += d;
		h4 += e;
	}

	std::array<uint8_t, 20> digest {};
	const uint32_t words[5] = { h0, h1, h2, h3, h4 };
	for (size_t i = 0; i < 5; ++i)
	{
		digest[(i * 4) + 0] = static_cast<uint8_t>(words[i] >> 24);
		digest[(i * 4) + 1] = static_cast<uint8_t>(words[i] >> 16);
		digest[(i * 4) + 2] = static_cast<uint8_t>(words[i] >> 8);
		digest[(i * 4) + 3] = static_cast<uint8_t>(words[i]);
	}

	return digest;
}

} // namespace

struct Rc4Decryptor::Impl
{
	std::array<uint8_t, Rc4KeyLength> key {};
};

Rc4Decryptor::Rc4Decryptor()  = default;

Rc4Decryptor::~Rc4Decryptor() = default;

bool Rc4Decryptor::Init(std::string_view passphrase)
{
	auto impl                       = std::make_unique<Impl>();
	const std::array<uint8_t, 20> h = Sha1(passphrase);
	std::copy_n(h.begin(), Rc4KeyLength, impl->key.begin());

	_impl = std::move(impl);
	return true;
}

void Rc4Decryptor::Release()
{
	_impl.reset();
}

bool Rc4Decryptor::Decrypt(void* data, size_t size)
{
	if (_impl == nullptr)
		return false;

	std::array<uint8_t, 256> s {};
	for (size_t i = 0; i < s.size(); ++i)
		s[i] = static_cast<uint8_t>(i);

	for (size_t i = 0, j = 0; i < s.size(); ++i)
	{
		j = (j + s[i] + _impl->key[i % Rc4KeyLength]) & 0xFF;
		std::swap(s[i], s[j]);
	}

	std::span<uint8_t> bytes(static_cast<uint8_t*>(data), size);
	for (size_t n = 0, i = 0, j = 0; n < size; ++n)
	{
		i = (i + 1) & 0xFF;
		j = (j + s[i]) & 0xFF;
		std::swap(s[i], s[j]);
		bytes[n] ^= s[(s[i] + s[j]) & 0xFF];
	}

	return true;
}

} // namespace Platform

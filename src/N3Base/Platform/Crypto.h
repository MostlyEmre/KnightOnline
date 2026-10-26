#ifndef N3BASE_PLATFORM_CRYPTO_H
#define N3BASE_PLATFORM_CRYPTO_H

#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

namespace Platform
{

/// \brief Decrypts data encrypted the way the client's data files are: RC4 with a 128-bit key
/// derived from the SHA-1 of a passphrase (CryptoAPI CryptDeriveKey), restarting the cipher on
/// every Decrypt() call (CryptDecrypt with Final = TRUE).
class Rc4Decryptor
{
public:
	Rc4Decryptor();
	~Rc4Decryptor();

	Rc4Decryptor(const Rc4Decryptor&)            = delete;
	Rc4Decryptor& operator=(const Rc4Decryptor&) = delete;

	/// \brief Derives the key. Returns false on failure.
	bool Init(std::string_view passphrase);

	/// \brief Forgets the key.
	void Release();

	/// \brief Decrypts in place. Returns false on failure.
	bool Decrypt(void* data, size_t size);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};

} // namespace Platform

#endif // N3BASE_PLATFORM_CRYPTO_H

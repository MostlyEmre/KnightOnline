#include "../Crypto.h"
#include "../Types.h"

#include <wincrypt.h>

namespace Platform
{

struct Rc4Decryptor::Impl
{
	HCRYPTPROV hCryptProvider = 0;
	HCRYPTHASH hCryptHash     = 0;
	HCRYPTKEY hCryptKey       = 0;
};

Rc4Decryptor::Rc4Decryptor() = default;

Rc4Decryptor::~Rc4Decryptor()
{
	Release();
}

bool Rc4Decryptor::Init(std::string_view passphrase)
{
	Release();

	auto impl = std::make_unique<Impl>();

	// Try to acquire an existing key context
	// NOTE: Officially this passes 0, but this will require access to the persistent keystore used for
	// private keys.
	// If we use CRYPT_VERIFYCONTEXT instead, we don't need access to private keys, so we can avoid
	// requiring the extra privs.
	if (!CryptAcquireContextA(
			&impl->hCryptProvider, nullptr, MS_ENHANCED_PROV_A, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)
		// Create a new key context instead
		&& !CryptAcquireContextA(
			&impl->hCryptProvider, nullptr, MS_ENHANCED_PROV_A, PROV_RSA_FULL, CRYPT_NEWKEYSET))
		return false;

	if (!CryptCreateHash(impl->hCryptProvider, CALG_SHA, 0, 0, &impl->hCryptHash))
	{
		CryptReleaseContext(impl->hCryptProvider, 0);
		return false;
	}

	if (!CryptHashData(impl->hCryptHash, reinterpret_cast<const BYTE*>(passphrase.data()),
			static_cast<DWORD>(passphrase.size()), 0)
		|| !CryptDeriveKey(
			impl->hCryptProvider, CALG_RC4, impl->hCryptHash, 0x800000u, &impl->hCryptKey))
	{
		CryptDestroyHash(impl->hCryptHash);
		CryptReleaseContext(impl->hCryptProvider, 0);
		return false;
	}

	_impl = std::move(impl);
	return true;
}

void Rc4Decryptor::Release()
{
	if (_impl == nullptr)
		return;

	CryptDestroyKey(_impl->hCryptKey);
	CryptDestroyHash(_impl->hCryptHash);
	CryptReleaseContext(_impl->hCryptProvider, 0);
	_impl.reset();
}

bool Rc4Decryptor::Decrypt(void* data, size_t size)
{
	if (_impl == nullptr)
		return false;

	DWORD dwDataLen = static_cast<DWORD>(size);
	return CryptDecrypt(_impl->hCryptKey, 0, TRUE, 0, static_cast<BYTE*>(data), &dwDataLen)
		   != FALSE;
}

} // namespace Platform

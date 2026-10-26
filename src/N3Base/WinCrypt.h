#ifndef N3BASE_WINCRYPT_H
#define N3BASE_WINCRYPT_H

#pragma once

#include "Platform/Crypto.h"

class File;
class CWinCrypt
{
public:
	constexpr const static char Cipher[] = "owsd9012%$1as!wpow1033b%!@%12";

	inline bool IsLoaded() const
	{
		return m_bIsLoaded;
	}

	CWinCrypt();
	~CWinCrypt();
	bool Load();
	void Release();
	bool ReadFile(File& file, void* buffer, size_t bytesToRead, size_t* bytesRead = nullptr);

protected:
	bool m_bIsLoaded;
	Platform::Rc4Decryptor m_Decryptor;
};

#endif // #define N3BASE_WINCRYPT_H

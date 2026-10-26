#include "StdAfxBase.h"
#include "WinCrypt.h"
#include "N3Base.h"

#include <FileIO/File.h>

CWinCrypt::CWinCrypt()
{
	m_bIsLoaded = false;
}

bool CWinCrypt::Load()
{
	Release();

	if (!m_Decryptor.Init(std::string_view(Cipher, sizeof(Cipher) - 1)))
		return false;

	m_bIsLoaded = TRUE;
	return true;
}

void CWinCrypt::Release()
{
	m_Decryptor.Release();
	m_bIsLoaded = false;
}

bool CWinCrypt::ReadFile(
	File& file, void* buffer, size_t bytesToRead, size_t* bytesRead /*= nullptr*/)
{
	if (!file.Read(buffer, bytesToRead, bytesRead))
		return false;

	if (IsLoaded())
	{
		if (!m_Decryptor.Decrypt(buffer, bytesToRead))
			return false;
	}

	return true;
}

CWinCrypt::~CWinCrypt()
{
	Release();
}

#include "StdAfxBase.h"
#include "N3TableBaseImpl.h"

#include <FileIO/FileReader.h>
#include <FileIO/MemoryReader.h>

#include <vector>

#ifdef _N3GAME
#include "LogWriter.h"
#endif

CN3TableBaseImpl::CN3TableBaseImpl()
{
}

CN3TableBaseImpl::~CN3TableBaseImpl()
{
}

bool CN3TableBaseImpl::LoadFromFile(const std::string& szFN)
{
	if (szFN.empty())
		return false;

	FileReader encryptedFile;
	if (!encryptedFile.OpenExisting(szFN))
	{
#ifdef _N3GAME
		CLogWriter::Write("N3TableBase - Can't open file(read) File Handle error ({})", szFN);
#endif
		return false;
	}

	// 파일 암호화 풀기..
	size_t encryptedFileSize = static_cast<size_t>(encryptedFile.Size());
	if (encryptedFileSize == 0)
	{
		encryptedFile.Close();
		return false;
	}

	// 원래 파일을 읽고..
	std::vector<uint8_t> datas(encryptedFileSize);
	encryptedFile.Read(datas.data(), encryptedFileSize); // 암호화된 데이터 읽고..
	encryptedFile.Close();                               // 원래 파일 닫고

														 // 테이블 만드는 툴에서 쓰는 키와 같은 키..
	uint16_t key_r  = 0x0816;
	uint16_t key_c1 = 0x6081;
	uint16_t key_c2 = 0x1608;

	//uint8_t Encrypt(uint8_t plain)
	//{
	//	uint8_t cipher;
	//	cipher = (plain ^ (key_r>>8));
	//	key_r = (cipher + key_r) * key_c1 + key_c2;
	//	return cipher;
	//}

	//uint8_t Decrypt(uint8_t cipher)
	//{
	//	uint8_t plain;
	//	plain = (cipher ^ (m_r>>8));
	//	m_r = (cipher + m_r) * m_c1 + m_c2;
	//	return plain;
	//}

	// 암호화 풀고..
	for (uint8_t& byCipher : datas)
	{
		uint8_t byData = (byCipher ^ (key_r >> 8));
		key_r          = (byCipher + key_r) * key_c1 + key_c2;
		byCipher       = byData;
	}

	// Read the decrypted table straight from memory (it used to go through a temporary file next
	// to the table, which also needed the data folder to be writable).
	MemoryReader decryptedFile;
	decryptedFile.Open(std::move(datas), szFN);

	bool bResult = Load(decryptedFile);
	decryptedFile.Close();

	if (!bResult)
	{
#ifdef _N3GAME
		CLogWriter::Write("N3TableBase - incorrect table ({})", szFN);
#endif
	}

	return bResult;
}

bool CN3TableBaseImpl::ReadData(File& file, DATA_TYPE DataType, void* pData)
{
	switch (DataType)
	{
		case DT_CHAR:
			file.Read(pData, sizeof(char));
			break;

		case DT_BYTE:
			file.Read(pData, sizeof(uint8_t));
			break;

		case DT_SHORT:
			file.Read(pData, sizeof(int16_t));
			break;

		case DT_WORD:
			file.Read(pData, sizeof(uint16_t));
			break;

		case DT_INT:
			file.Read(pData, sizeof(int));
			break;

		case DT_DWORD:
			file.Read(pData, sizeof(uint32_t));
			break;

		case DT_STRING:
		{
			std::string& szString = *((std::string*) pData);

			int iStrLen           = 0;
			file.Read(&iStrLen, sizeof(iStrLen));

			szString.clear();
			if (iStrLen > 0)
			{
				szString.assign(iStrLen, ' ');
				file.Read(&szString[0], iStrLen);
			}
		}
		break;

		case DT_FLOAT:
			file.Read(pData, sizeof(float));
			break;

		case DT_DOUBLE:
			file.Read(pData, sizeof(double));
			break;

		case DT_NONE:
		default:
			__ASSERT(0, "");
			return false;
	}

	return true;
}

int CN3TableBaseImpl::SizeOf(DATA_TYPE DataType) const
{
	switch (DataType)
	{
		case DT_CHAR:
			return sizeof(char);

		case DT_BYTE:
			return sizeof(uint8_t);

		case DT_SHORT:
			return sizeof(int16_t);

		case DT_WORD:
			return sizeof(uint16_t);

		case DT_INT:
			return sizeof(int);

		case DT_DWORD:
			return sizeof(uint32_t);

		case DT_STRING:
			return sizeof(std::string);

		case DT_FLOAT:
			return sizeof(float);

		case DT_DOUBLE:
			return sizeof(double);

		default:
			break;
	}

	__ASSERT(0, "");
	return 0;
}

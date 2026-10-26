#include "MemoryReader.h"

#include <algorithm>
#include <cassert>
#include <cstdio>  // SEEK_SET, SEEK_CUR, SEEK_END
#include <cstring> // std::memcpy()

bool MemoryReader::Open(std::vector<uint8_t> data, const std::filesystem::path& path)
{
	Close();

	_data   = std::move(data);
	_size   = _data.size();
	_offset = 0;
	_path   = path;
	_open   = true;
	return true;
}

bool MemoryReader::OpenExisting(const std::filesystem::path& /*path*/)
{
	return false;
}

bool MemoryReader::Create(const std::filesystem::path& /*path*/)
{
	return false;
}

bool MemoryReader::Read(void* buffer, size_t bytesToRead, size_t* bytesRead /*= nullptr*/)
{
	// Same contract as FileReader::Read().
	if (bytesRead != nullptr)
		*bytesRead = 0;

	if (buffer == nullptr)
		return false;

	if (bytesToRead == 0)
		return true;

	assert(_open);
	assert(_offset <= _size);

	const size_t bytesToCopy = std::min(bytesToRead, static_cast<size_t>(_size - _offset));
	if (bytesToCopy == 0)
		return false;

	std::memcpy(buffer, &_data[static_cast<size_t>(_offset)], bytesToCopy);
	_offset += bytesToCopy;

	if (bytesRead != nullptr)
		*bytesRead = bytesToCopy;

	return true;
}

bool MemoryReader::Write(
	const void* /*buffer*/, size_t /*bytesToWrite*/, size_t* /*bytesWritten = nullptr*/)
{
	return false;
}

bool MemoryReader::Seek(int64_t offset, int origin)
{
	if (!IsOpen())
		return false;

	int64_t newOffset = offset;
	switch (origin)
	{
		case SEEK_SET:
			break;

		case SEEK_CUR:
			newOffset += static_cast<int64_t>(_offset);
			break;

		case SEEK_END:
			newOffset += static_cast<int64_t>(_size);
			break;

		default:
			return false;
	}

	if (newOffset < 0 || static_cast<uint64_t>(newOffset) > _size)
		return false;

	_offset = static_cast<uint64_t>(newOffset);
	return true;
}

void MemoryReader::Flush()
{
}

bool MemoryReader::Close()
{
	if (!_open)
		return false;

	_data.clear();
	_data.shrink_to_fit();
	_size   = 0;
	_offset = 0;
	_path.clear();
	_open = false;
	return true;
}

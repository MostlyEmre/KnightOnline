#ifndef FILEIO_MEMORYREADER_H
#define FILEIO_MEMORYREADER_H

#pragma once

#include "File.h"

#include <cstdint>
#include <vector>

/// \brief MemoryReader implements a read-only file interface over bytes held in memory, such as a
/// file that was decrypted after reading it.
class MemoryReader : public File
{
public:
	MemoryReader() = default;

	/// \brief Takes the bytes to read; Path() reports the given path.
	/// \param data The file's contents.
	/// \param path Path the contents came from.
	/// \returns true.
	bool Open(std::vector<uint8_t> data, const std::filesystem::path& path);

	/// \brief Not supported for MemoryReader. Always returns false.
	bool OpenExisting(const std::filesystem::path& path) override;

	/// \brief Not supported for MemoryReader. Always returns false.
	bool Create(const std::filesystem::path& path) override;

	/// \brief Reads data at the current offset.
	/// \param buffer Destination buffer for data.
	/// \param bytesToRead Number of bytes to read.
	/// \param bytesRead Optional output for number of bytes actually read.
	/// \returns true if at least one byte was read successfully, false otherwise.
	bool Read(void* buffer, size_t bytesToRead, size_t* bytesRead = nullptr) override;

	/// \brief Writing is not supported for MemoryReader.
	/// \returns false.
	bool Write(const void* buffer, size_t bytesToWrite, size_t* bytesWritten = nullptr) override;

	/// \brief Changes the current offset.
	/// \param offset Offset value.
	/// \param origin One of SEEK_SET, SEEK_CUR, SEEK_END.
	/// \returns true if the new offset is within the data, false otherwise.
	bool Seek(int64_t offset, int origin) override;

	/// \brief No-op for MemoryReader, as it does not write any data.
	void Flush() override;

	/// \brief Releases the data.
	/// \returns true if it was open, false otherwise.
	bool Close() override;

private:
	std::vector<uint8_t> _data;
};

#endif // FILEIO_MEMORYREADER_H

#ifndef N3BASE_PLATFORM_SETTINGS_H
#define N3BASE_PLATFORM_SETTINGS_H

#pragma once

#include <cstddef>
#include <string>

// Small per-user binary settings, grouped under a key (the registry under HKEY_CURRENT_USER on
// Windows, e.g. "Software\\KnightOnline\\...").
namespace Platform
{

/// \brief Stores a value, creating the key if needed. Returns false on failure.
bool WriteUserSetting(
	const std::string& szKey, const char* szValueName, const void* pData, size_t size);

/// \brief Reads up to `size` bytes of a value. Returns false if the key or value doesn't exist.
bool ReadUserSetting(const std::string& szKey, const char* szValueName, void* pData, size_t size);

} // namespace Platform

#endif // N3BASE_PLATFORM_SETTINGS_H

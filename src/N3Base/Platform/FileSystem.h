#ifndef N3BASE_PLATFORM_FILESYSTEM_H
#define N3BASE_PLATFORM_FILESYSTEM_H

#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace Platform
{

/// \brief The names of the files directly in a directory whose extension (e.g. ".dxt") matches,
/// ignoring case. The directory may use '\' or '/' separators.
std::vector<std::string> FindFiles(const std::string& szDirectory, const std::string& szExtension);

/// \brief fopen() for a path as the game spells it ('\' separators, any case; see
/// FileIO/PathResolver.h). Returns nullptr on failure, like fopen().
FILE* OpenFile(const std::string& szPath, const char* szMode);

} // namespace Platform

#endif // N3BASE_PLATFORM_FILESYSTEM_H

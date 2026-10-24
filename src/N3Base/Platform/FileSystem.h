#ifndef N3BASE_PLATFORM_FILESYSTEM_H
#define N3BASE_PLATFORM_FILESYSTEM_H

#pragma once

#include <string>
#include <vector>

namespace Platform
{

/// \brief The names of the files directly in a directory whose extension (e.g. ".dxt") matches,
/// ignoring case. The directory may use '\' or '/' separators.
std::vector<std::string> FindFiles(const std::string& szDirectory, const std::string& szExtension);

} // namespace Platform

#endif // N3BASE_PLATFORM_FILESYSTEM_H

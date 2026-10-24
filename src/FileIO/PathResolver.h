#ifndef FILEIO_PATHRESOLVER_H
#define FILEIO_PATHRESOLVER_H

#pragma once

#include <filesystem>

/// \brief Maps paths as the game spells them to the files on disk.
///
/// The game spells paths the Windows way: '\' separators and any letter case (the client even
/// lowercases its base directory). On Windows that is how the file system behaves anyway, so
/// paths pass through unchanged. Elsewhere, '\' becomes '/', and a component that doesn't exist as
/// spelt is matched case-insensitively against the entries of its directory; whatever doesn't
/// exist yet (a file about to be created) keeps its spelling.
namespace PathResolver
{

/// \brief The path to use for opening `path` (unchanged on Windows).
std::filesystem::path Resolve(const std::filesystem::path& path);

} // namespace PathResolver

#endif // FILEIO_PATHRESOLVER_H

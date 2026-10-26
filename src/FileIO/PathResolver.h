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

/// \brief The path to use for opening `path` to read it (unchanged on Windows).
std::filesystem::path Resolve(const std::filesystem::path& path);

/// \brief The path to use for creating, writing or deleting `path`: like Resolve(), but never
/// inside the overlay's data directory (unchanged on Windows).
std::filesystem::path ResolveForWriting(const std::filesystem::path& path);

/// \brief Lays a writable directory over read-only game data (does nothing on Windows).
///
/// Afterwards a path that doesn't exist, and is relative (to the working directory, which should
/// be `userDir`) or inside `userDir`, is read from the same place under `dataDir` instead. The game
/// then reads its data from `dataDir` while its own files (logs, screenshots, options) are created
/// and found in `userDir`, which is how the macOS app uses its bundle and Application Support.
/// Call it before the game opens any file.
void SetOverlay(const std::filesystem::path& userDir, const std::filesystem::path& dataDir);

} // namespace PathResolver

#endif // FILEIO_PATHRESOLVER_H

#ifndef N3BASE_PLATFORM_MACOS_NOTIMPLEMENTED_H
#define N3BASE_PLATFORM_MACOS_NOTIMPLEMENTED_H

#pragma once

namespace Platform::macOS
{

/// \brief Logs (once per function) that a platform function isn't implemented on macOS yet.
void NotImplemented(const char* szFunction);

} // namespace Platform::macOS

#endif // N3BASE_PLATFORM_MACOS_NOTIMPLEMENTED_H

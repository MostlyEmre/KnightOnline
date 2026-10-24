#ifndef N3BASE_PLATFORM_RENDERAPI_H
#define N3BASE_PLATFORM_RENDERAPI_H

#pragma once

// The rendering API the engine is written against: Direct3D 9 (and the D3DX helpers it
// uses). On Windows this is the real thing; elsewhere it is the same API surface backed by
// the platform renderer (Compat/D3D9.h), so call sites don't change.

#if defined(_WIN32)
#include <d3dx9.h>
#else
#include "Compat/D3D9.h"
#endif

#endif // N3BASE_PLATFORM_RENDERAPI_H

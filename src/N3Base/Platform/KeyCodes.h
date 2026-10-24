#ifndef N3BASE_PLATFORM_KEYCODES_H
#define N3BASE_PLATFORM_KEYCODES_H

#pragma once

// DirectInput keyboard scan codes (DIK_*), which the engine uses as key-state indices on every
// platform (see Platform/Input.h: KeyboardDevice).

#if defined(_WIN32)
#include <dinput.h>
#else
#include "Compat/DInputKeys.h"
#endif

#endif // N3BASE_PLATFORM_KEYCODES_H

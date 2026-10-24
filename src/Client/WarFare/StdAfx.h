#ifndef CLIENT_WARFARE_STDAFX_H
#define CLIENT_WARFARE_STDAFX_H

#pragma once

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h> // htons/ntohs, which winsock2.h provides on Windows
#endif
#include <N3Base/My_3DStruct.h>

#if !__has_include(<warfare_config.h>)
#error warfare_config.h missing - copy and rename warfare_config.h.default to warfare_config.h
#endif

#include <warfare_config.h>

#endif // CLIENT_WARFARE_STDAFX_H

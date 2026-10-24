#ifndef N3BASE_PLATFORM_SOCKET_H
#define N3BASE_PLATFORM_SOCKET_H

#pragma once

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Platform
{

/// \brief A TCP client socket. Connecting blocks; afterwards the socket reports Connected/Readable/
/// Closed through the event loop (EventHandlers::OnSocketEvent, on the main thread).
class TcpSocket
{
public:
	TcpSocket();
	~TcpSocket();

	TcpSocket(const TcpSocket&)            = delete;
	TcpSocket& operator=(const TcpSocket&) = delete;

	/// \brief Connects to host (a dotted IPv4 address or a host name). Returns 0 on success, -1 if
	/// the host can't be resolved, or the system's error code.
	int Connect(HWND hWndNotify, const std::string& szHost, uint16_t port, int iRecvBufferSize);

	/// \brief Closes the socket (no-op if it isn't open).
	void Close();

	bool IsOpen() const;

	/// \brief The number of bytes that can be received without blocking.
	size_t AvailableBytes() const;

	/// \brief Receives up to `size` bytes. Returns the count, or -1 on error (see LastError()).
	int Receive(char* buffer, int size);

	/// \brief Sends up to `size` bytes. Returns the count, or -1 on error (see LastError()).
	int Send(const char* buffer, int size);

	/// \brief The last system error code of this thread.
	static int LastError();

	/// \brief Opaque; each platform's implementation defines it.
	struct Impl;

private:
	std::unique_ptr<Impl> _impl;
};

} // namespace Platform

#endif // N3BASE_PLATFORM_SOCKET_H

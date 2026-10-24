// winsock2.h must come before <windows.h> (which the platform headers include); this file is
// therefore built without the precompiled header.
// The client keeps the WinSock 1.1 calls it always used (inet_addr, gethostbyname, WSAAsyncSelect).
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>

#include "../Socket.h"
#include "Win32Internal.h"

#include <spdlog/fmt/bundled/format.h>

#include <cstring>

namespace Platform
{

namespace
{

WSAData s_WSData;
int s_nInstanceCount = 0;

} // namespace

struct TcpSocket::Impl
{
	SOCKET hSocket = INVALID_SOCKET;
};

TcpSocket::TcpSocket() : _impl(std::make_unique<Impl>())
{
	if (s_nInstanceCount++ == 0)
		(void) WSAStartup(0x0101, &s_WSData);
}

TcpSocket::~TcpSocket()
{
	Close();

	if (--s_nInstanceCount == 0)
		WSACleanup();
}

int TcpSocket::Connect(
	HWND hWndNotify, const std::string& szHost, uint16_t port, int iRecvBufferSize)
{
	Close();

	//
	struct sockaddr_in server {};
	struct hostent* hp = nullptr;

	if ((szHost[0] >= '0') && (szHost[0] <= '9'))
	{
		server.sin_family      = AF_INET;
		server.sin_addr.s_addr = inet_addr(szHost.c_str());
		server.sin_port        = htons(port);
	}
	else
	{
		hp = gethostbyname(szHost.c_str());
		if (hp == nullptr)
		{
#ifdef _DEBUG
			std::string msg = fmt::format("Error: Connecting to {}.", szHost);
			MessageBoxA(hWndNotify, msg.c_str(), "socket error", MB_OK | MB_ICONSTOP);
#endif
			return -1;
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): h_addr is h_addr_list[0]
		memcpy(&server.sin_addr, hp->h_addr, hp->h_length);
		server.sin_family = hp->h_addrtype;
		server.sin_port   = htons(port);
	} // else

	// create socket
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
	{
		int iErrCode = ::WSAGetLastError();
#ifdef _DEBUG
		char msg[] = "Error opening stream socket";
		MessageBoxA(hWndNotify, msg, "socket error", MB_OK | MB_ICONSTOP);
#endif
		return iErrCode;
	}

	_impl->hSocket = sock;

	// 소켓 옵션
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&iRecvBufferSize), 4);

	if (connect(sock, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) != 0)
	{
		int iErrCode = ::WSAGetLastError();

		closesocket(sock);
		_impl->hSocket = INVALID_SOCKET;

		return iErrCode;
	}

	WSAAsyncSelect(sock, hWndNotify, Win32::WM_SOCKETMSG, FD_CONNECT | FD_READ | FD_CLOSE);
	return 0;
}

void TcpSocket::Close()
{
	if (_impl->hSocket != INVALID_SOCKET)
		closesocket(_impl->hSocket);

	_impl->hSocket = INVALID_SOCKET;
}

bool TcpSocket::IsOpen() const
{
	return _impl->hSocket != INVALID_SOCKET;
}

size_t TcpSocket::AvailableBytes() const
{
	u_long dwPktSize = 0;
	ioctlsocket(_impl->hSocket, FIONREAD, &dwPktSize);
	return dwPktSize;
}

int TcpSocket::Receive(char* buffer, int size)
{
	int count = recv(_impl->hSocket, buffer, size, 0);
	return count == SOCKET_ERROR ? -1 : count;
}

int TcpSocket::Send(const char* buffer, int size)
{
	int count = send(_impl->hSocket, buffer, size, 0);
	return count == SOCKET_ERROR ? -1 : count;
}

int TcpSocket::LastError()
{
	return static_cast<int>(::GetLastError());
}

} // namespace Platform

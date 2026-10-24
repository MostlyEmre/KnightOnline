#include "../Socket.h"
#include "NotImplemented.h"

#include <cerrno>

namespace Platform
{

struct TcpSocket::Impl
{
};

TcpSocket::TcpSocket()  = default;

TcpSocket::~TcpSocket() = default;

int TcpSocket::Connect(
	HWND /*hWndNotify*/, const std::string& /*szHost*/, uint16_t /*port*/, int /*iRecvBufferSize*/)
{
	macOS::NotImplemented("TcpSocket::Connect");
	return ENOSYS;
}

void TcpSocket::Close()
{
}

bool TcpSocket::IsOpen() const
{
	return false;
}

size_t TcpSocket::AvailableBytes() const
{
	return 0;
}

int TcpSocket::Receive(char* /*buffer*/, int /*size*/)
{
	return -1;
}

int TcpSocket::Send(const char* /*buffer*/, int /*size*/)
{
	return -1;
}

int TcpSocket::LastError()
{
	return errno;
}

} // namespace Platform

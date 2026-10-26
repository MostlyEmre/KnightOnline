#include "../Socket.h"
#include "MacState.h"

#include <asio.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

namespace Platform
{

// The socket connects and transfers with asio's blocking calls. Instead of WSAAsyncSelect's
// window messages, the event loop asks every open socket each frame whether it has data or was
// closed (DispatchSocketEvents), on the main thread like the messages were.

namespace
{

asio::io_context& Context()
{
	static asio::io_context context;
	return context;
}

} // namespace

struct TcpSocket::Impl
{
	asio::ip::tcp::socket socket { Context() };
	bool bClosedReported = false;
};

namespace
{

std::vector<TcpSocket::Impl*> s_openSockets;

void Unregister(TcpSocket::Impl* impl)
{
	std::erase(s_openSockets, impl);
}

} // namespace

TcpSocket::TcpSocket() : _impl(std::make_unique<Impl>())
{
}

TcpSocket::~TcpSocket()
{
	Close();
}

int TcpSocket::Connect(
	HWND /*hWndNotify*/, const std::string& szHost, uint16_t port, int iRecvBufferSize)
{
	Close();

	asio::error_code ec;
	asio::ip::tcp::endpoint endpoint;

	if (!szHost.empty() && szHost[0] >= '0' && szHost[0] <= '9')
	{
		endpoint = asio::ip::tcp::endpoint(asio::ip::make_address_v4(szHost, ec), port);
		if (ec)
			return -1;
	}
	else
	{
		// A blocking lookup, like gethostbyname in the Windows version.
		addrinfo hints {};
		hints.ai_family   = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* result  = nullptr;
		if (getaddrinfo(szHost.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
			return -1;

		sockaddr_in address {};
		std::memcpy(&address, result->ai_addr, sizeof(address));
		freeaddrinfo(result);
		endpoint = asio::ip::tcp::endpoint(
			asio::ip::address_v4(ntohl(address.sin_addr.s_addr)), port);
	}

	_impl->socket.open(asio::ip::tcp::v4(), ec);
	if (ec)
		return ec.value();

	_impl->socket.set_option(asio::socket_base::receive_buffer_size(iRecvBufferSize), ec);

	_impl->socket.connect(endpoint, ec);
	if (ec)
	{
		asio::error_code ignored;
		_impl->socket.close(ignored);
		return ec.value();
	}

	_impl->bClosedReported = false;
	s_openSockets.push_back(_impl.get());
	return 0;
}

void TcpSocket::Close()
{
	Unregister(_impl.get());

	if (_impl->socket.is_open())
	{
		asio::error_code ignored;
		_impl->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
		_impl->socket.close(ignored);
	}
}

bool TcpSocket::IsOpen() const
{
	return _impl->socket.is_open();
}

size_t TcpSocket::AvailableBytes() const
{
	asio::error_code ec;
	size_t available = _impl->socket.available(ec);
	return ec ? 0 : available;
}

int TcpSocket::Receive(char* buffer, int size)
{
	asio::error_code ec;
	size_t count = _impl->socket.read_some(asio::buffer(buffer, static_cast<size_t>(size)), ec);
	if (ec == asio::error::eof)
		return 0;
	if (ec)
	{
		errno = ec.value();
		return -1;
	}
	return static_cast<int>(count);
}

int TcpSocket::Send(const char* buffer, int size)
{
	asio::error_code ec;
	size_t count = _impl->socket.write_some(asio::buffer(buffer, static_cast<size_t>(size)), ec);
	if (ec)
	{
		errno = ec.value();
		return -1;
	}
	return static_cast<int>(count);
}

int TcpSocket::LastError()
{
	return errno;
}

namespace macOS
{

void DispatchSocketEvents()
{
	const EventHandlers& handlers = Handlers();
	if (!handlers.OnSocketEvent)
		return;

	// Handlers may close sockets (and so change the list) while we go.
	std::vector<TcpSocket::Impl*> sockets = s_openSockets;
	for (TcpSocket::Impl* impl : sockets)
	{
		if (std::find(s_openSockets.begin(), s_openSockets.end(), impl) == s_openSockets.end())
			continue;

		pollfd fd {};
		fd.fd     = impl->socket.native_handle();
		fd.events = POLLIN;
		if (poll(&fd, 1, 0) <= 0 || (fd.revents & (POLLIN | POLLHUP | POLLERR)) == 0)
			continue;

		asio::error_code ec;
		if (impl->socket.available(ec) > 0 && !ec)
		{
			handlers.OnSocketEvent(SocketEvent::Readable);
		}
		else if (!impl->bClosedReported)
		{
			// Readable with nothing to read: the peer closed the connection.
			impl->bClosedReported = true;
			handlers.OnSocketEvent(SocketEvent::Closed);
		}
	}
}

} // namespace macOS

} // namespace Platform

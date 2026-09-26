/*
 * Copyright 2010-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
 */

#include "bnet_p.h"

#include <bx/endian.h>
#include <bx/allocator.h>

namespace bnet
{
	static bx::DefaultAllocator s_allocatorStub;
	bx::AllocatorI* g_allocator = &s_allocatorStub;


	int getLastError()
	{
#if BX_PLATFORM_WINDOWS
		return WSAGetLastError();
#elif  BX_PLATFORM_LINUX \
	|| BX_PLATFORM_ANDROID \
	|| BX_PLATFORM_EMSCRIPTEN \
	|| BX_PLATFORM_OSX \
	|| BX_PLATFORM_IOS
		return errno;
#else
		return 0;
#endif // BX_PLATFORM_
	}

	bool isInProgress()
	{
#if BX_PLATFORM_WINDOWS
		return WSAEINPROGRESS == getLastError();
#else
		return EINPROGRESS == getLastError();
#endif // BX_PLATFORM_WINDOWS
	}

	bool isWouldBlock()
	{
#if BX_PLATFORM_WINDOWS
		return WSAEWOULDBLOCK == getLastError();
#else
		return EWOULDBLOCK == getLastError();
#endif // BX_PLATFORM_WINDOWS
	}

	void setNonBlock(SOCKET _socket)
	{
#if BX_PLATFORM_WINDOWS
		unsigned long opt = 1 ;
		::ioctlsocket(_socket, FIONBIO, &opt);
#elif  BX_PLATFORM_LINUX \
	|| BX_PLATFORM_ANDROID \
	|| BX_PLATFORM_EMSCRIPTEN \
	|| BX_PLATFORM_OSX \
	|| BX_PLATFORM_IOS
		::fcntl(_socket, F_SETFL, O_NONBLOCK);
#else
		BX_UNUSED(_socket);
#endif // BX_PLATFORM_
	}

	static void setSockOpts(SOCKET _socket)
	{
		int result;

		int win = 256<<10;
		result = ::setsockopt(_socket, SOL_SOCKET, SO_RCVBUF, (char*)&win, sizeof(win) );
		result = ::setsockopt(_socket, SOL_SOCKET, SO_SNDBUF, (char*)&win, sizeof(win) );

		int noDelay = 1;
		result = ::setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, sizeof(noDelay) );
		BX_UNUSED(result);
	}

	static int connectsocket(SOCKET socket, uint32_t _ip, uint16_t _port, bool /*_secure*/)
	{
		sockaddr_in addr;
		bx::memSet(&addr, 0, sizeof(addr) );
		addr.sin_family      = AF_INET;
		addr.sin_addr.s_addr = htonl(_ip);
		addr.sin_port        = htons(_port);

		union
		{
			sockaddr*    sa;
			sockaddr_in* sain;
		} saintosa;

		saintosa.sain = &addr;
	
		return ::connect(socket, saintosa.sa, sizeof(addr) );
	}

	static bool issocketready(SOCKET socket)
	{
		fd_set rfds;
		FD_ZERO(&rfds);
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(socket, &rfds);
		FD_SET(socket, &wfds);

		timeval timeout;
		timeout.tv_sec  = 0;
		timeout.tv_usec = 0;

		int result = ::select( (int)socket + 1 /*nfds is ignored on windows*/, &rfds, &wfds, NULL, &timeout);
		return result > 0;
	}

	class Connection
	{
	public:
		Connection()
			: m_socket(INVALID_SOCKET)
			, m_handle(invalidHandle)
			, m_incomingBuffer( (uint8_t*)bx::alloc(g_allocator, BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE) )
			, m_incoming(BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE)
			, m_recv(m_incoming, (char*)m_incomingBuffer)
			, m_incomingMsg(NULL)
#if BNET_CONFIG_TLS
			, m_tls(NULL)
#endif // BNET_CONFIG_TLS
			, m_len(-1)
			, m_raw(false)
			, m_tcpHandshake(true)
			, m_tlsHandshake(false)
		{
		}

		~Connection()
		{
			bx::free(g_allocator, m_incomingBuffer);
		}

		void connect(Handle _handle, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx, const char* _hostname)
		{
			init(_handle, _raw);

			m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (INVALID_SOCKET == m_socket)
			{
				ctxPush(m_handle, MessageId::ConnectFailed);
				return;
			}

			setSockOpts(m_socket);
			setNonBlock(m_socket);

			const bool secure = _tlsCtx != NULL;
			int err = connectsocket(m_socket, _ip, _port, secure);

			if (0 != err
			&&  !(isInProgress() || isWouldBlock() ) )
			{
				BX_TRACE("Connect %d - Connect failed. %d", m_handle, getLastError() );

				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;

				ctxPush(m_handle, MessageId::ConnectFailed);
				return;
			}

#if BNET_CONFIG_TLS
			if (secure)
			{
				m_tls = tlsConnect(_tlsCtx, m_socket, _hostname);
				if (NULL == m_tls)
				{
					BX_TRACE("Connect %d - TLS session create failed.", m_handle);
					::closesocket(m_socket);
					m_socket = INVALID_SOCKET;
					ctxPush(m_handle, MessageId::ConnectFailed);
					return;
				}

				m_tlsHandshake = true;
			}
#else
			BX_UNUSED(_tlsCtx, _hostname);
#endif // BNET_CONFIG_TLS
		}

		bool accept(Handle _handle, Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx)
		{
			init(_handle, _raw);

			m_socket = _socket;

			setSockOpts(m_socket);
			setNonBlock(m_socket);

			// Socket returned by accept is already connected.
			m_tcpHandshake = false;

			Message* msg = msgAlloc(m_handle, 9, true);
			msg->data[0] = MessageId::IncomingConnection;
			*( (uint16_t*)&msg->data[1]) = _listenHandle.idx;
			*( (uint32_t*)&msg->data[3]) = _ip;
			*( (uint16_t*)&msg->data[7]) = _port;

#if BNET_CONFIG_TLS
			if (NULL != _tlsCtx)
			{
				m_tls = tlsAccept(_tlsCtx, m_socket);

				if (NULL == m_tls)
				{
					BX_TRACE("Accept %d - TLS session create failed.", m_handle.idx);

					msgRelease(msg);
					::closesocket(m_socket);
					m_socket = INVALID_SOCKET;

					return false;
				}

				m_tlsHandshake = true;

				// Notify about incoming connection only once TLS handshake is complete.
				m_incomingMsg = msg;

				return true;
			}
#else
			BX_UNUSED(_tlsCtx);
#endif // BNET_CONFIG_TLS

			ctxPush(msg);

			return true;
		}

		void disconnect(DisconnectReason::Enum _reason = DisconnectReason::None)
		{
#if BNET_CONFIG_TLS
			if (NULL != m_tls)
			{
				tlsDestroy(m_tls);
				m_tls = NULL;
			}
#endif // BNET_CONFIG_TLS

			m_tlsHandshake = false;

			if (NULL != m_incomingMsg)
			{
				msgRelease(m_incomingMsg);
				m_incomingMsg = NULL;
			}

			if (INVALID_SOCKET != m_socket)
			{
				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;
			}

			for (Message* msg = m_outgoing.pop(); NULL != msg; msg = m_outgoing.pop() )
			{
				release(msg);
			}

			if (_reason != DisconnectReason::None)
			{
				Message* msg = msgAlloc(m_handle, 2, true);
				msg->data[0] = MessageId::LostConnection;
				msg->data[1] = uint8_t(_reason);
				ctxPush(msg);
			}
		}

		void send(Message* _msg)
		{
			BX_ASSERT(m_raw || _msg->data[0] >= MessageId::UserDefined, "Sending message with MessageId below UserDefined is not allowed!");
			if (INVALID_SOCKET != m_socket)
			{
				m_outgoing.push(_msg);
				update();
			}
		}

		void update()
		{
			if (INVALID_SOCKET != m_socket)
			{
				updateSocket();

				if (!m_tcpHandshake
				&&  !m_tlsHandshake)
				{
					updateIncomingMessages();
				}
			}
		}

		bool hasSocket() const
		{
			return INVALID_SOCKET != m_socket;
		}

	private:
		void init(Handle _handle, bool _raw)
		{
			m_handle = _handle;
			m_tcpHandshake = true;
			m_tlsHandshake = false;
			m_tcpHandshakeTimeout = bx::getHPCounter() + bx::getHPFrequency()*BNET_CONFIG_CONNECT_TIMEOUT_SECONDS;
			m_incomingMsg = NULL;
			m_len = -1;
			m_raw = _raw;
		}

		void read(bx::WriteRingBuffer& _out, uint32_t _len)
		{
			bx::ReadRingBuffer incoming(m_incoming, (char*)m_incomingBuffer, _len);
			_out.write(incoming, _len);
			incoming.end();
		}

		void read(uint32_t _len)
		{
			m_incoming.consume(_len);
		}

		void read(char* _data, uint32_t _len)
		{
			bx::ReadRingBuffer incoming(m_incoming, (char*)m_incomingBuffer, _len);
			incoming.read(_data, _len);
			incoming.end();
		}

		void peek(char* _data, uint32_t _len)
		{
			bx::ReadRingBuffer incoming(m_incoming, (char*)m_incomingBuffer, _len);
			incoming.read(_data, _len);
		}

		void updateIncomingMessages()
		{
			if (m_raw)
			{
				uint16_t available = uint16_t(bx::min<uint32_t>(m_incoming.getNumUsed(), maxMessageSize-1) );

				if (0 < available)
				{
					Message* msg = msgAlloc(m_handle, available+1, true);
					msg->data[0] = MessageId::RawData;
					read( (char*)&msg->data[1], available);
					ctxPush(msg);
				}
			}
			else
			{
				uint32_t available = bx::min<uint32_t>(m_incoming.getNumUsed(), maxMessageSize);

				while (0 < available)
				{
					if (-1 == m_len)
					{
						if (2 > available)
						{
							return;
						}
						else
						{
							uint16_t len;
							read((char*)&len, 2);
							m_len = bx::toHostEndian(len, true);
						}
					}
					else
					{
						if (m_len > int(available) )
						{
							return;
						}
						else
						{
							Message* msg = msgAlloc(m_handle, uint16_t(m_len), true);
							read( (char*)msg->data, m_len);
							uint8_t id = msg->data[0];

							if (id < MessageId::UserDefined)
							{
								msgRelease(msg);

								BX_TRACE("Disconnect %d - Invalid message id.", m_handle);
								disconnect(DisconnectReason::InvalidMessageId);
								return;
							}

							ctxPush(msg);

							m_len = -1;
						}
					}

					available = bx::min<uint32_t>(m_incoming.getNumUsed(), maxMessageSize);
				}
			}
		}

		void updateSocket()
		{
			if (updateTcpHandshake()
			&&  updateTlsHandshake() )
			{
				if (m_tlsHandshake)
				{
					return;
				}

				int bytes;

#if BNET_CONFIG_TLS
				if (NULL != m_tls)
				{
					bytes = m_recv.recv(m_tls);
				}
				else
#endif // BNET_CONFIG_TLS
				{
					bytes = m_recv.recv(m_socket);
				}

				if (1 > bytes)
				{
					if (0 == bytes)
					{
						BX_TRACE("Disconnect %d - Host closed connection.", m_handle);
						disconnect(DisconnectReason::HostClosed);
						return;
					}
					else if (!isWouldBlock() )
					{
						BX_TRACE("Disconnect %d - Receive failed. %d", m_handle, getLastError() );
						disconnect(DisconnectReason::RecvFailed);
						return;
					}
				}

				if (!m_tlsHandshake)
				{
					if (m_raw)
					{
						for (Message* msg = m_outgoing.peek(); NULL != msg; msg = m_outgoing.peek() )
						{
							Internal::Enum id = Internal::Enum(*(msg->data - 2) );
							if (Internal::None != id)
							{
								if (!processInternal(id, msg) )
								{
									return;
								}
							}
							else if (!send( (char*)msg->data, msg->size) )
							{
								return;
							}

							release(m_outgoing.pop() );
						}
					}
					else
					{
						for (Message* msg = m_outgoing.peek(); NULL != msg; msg = m_outgoing.peek() )
						{
							Internal::Enum id = Internal::Enum(*(msg->data - 2) );
							if (Internal::None != id)
							{
								*( (uint16_t*)msg->data - 1) = msg->size;
								if (!processInternal(id, msg) )
								{
									return;
								}
							}
							else
							{
								*( (uint16_t*)msg->data - 1) = bx::toLittleEndian(msg->size);
								if (!send( (char*)msg->data - 2, msg->size+2) )
								{
									return;
								}
							}

							release(m_outgoing.pop() );
						}
					}
				}
			}
		}

		bool processInternal(Internal::Enum _id, Message* _msg)
		{
			switch (_id)
			{
			case Internal::Disconnect:
				{
					Message* msg = msgAlloc(_msg->handle, 2, true);
					msg->data[0] = 0;
					msg->data[1] = Internal::Disconnect;
					ctxPush(msg);

					BX_TRACE("Disconnect %d - Client closed connection (finish).", m_handle);
					disconnect();
				}
				return false;

			case Internal::Notify:
				{
					Message* msg = msgAlloc(_msg->handle, _msg->size+1, true);
					msg->data[0] = MessageId::Notify;
					bx::memCopy(&msg->data[1], _msg->data, _msg->size);
					ctxPush(msg);
				}
				return true;

			default:
				break;
			}

			BX_ASSERT(false, "You should not be here!");
			return true;
		}

		bool updateTcpHandshake()
		{
			if (!m_tcpHandshake)
			{
				return true;
			}

			uint64_t now = bx::getHPCounter();
			if (now > m_tcpHandshakeTimeout)
			{
				BX_TRACE("Disconnect %d - Connect timeout.", m_handle);
				ctxPush(m_handle, MessageId::ConnectFailed);
				disconnect();
				return false;
			}

			if (!issocketready(m_socket) )
			{
				return false;
			}

			int error = 0;
			socklen_t len = sizeof(error);
			if (0 != ::getsockopt(m_socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len)
			||  0 != error)
			{
				BX_TRACE("Disconnect %d - Connect failed. %d", m_handle.idx, error);
				ctxPush(m_handle, MessageId::ConnectFailed);
				disconnect();
				return false;
			}

			m_tcpHandshake = false;
			return true;
		}

		bool updateTlsHandshake()
		{
#if BNET_CONFIG_TLS
			if (NULL != m_tls
			&&  m_tlsHandshake)
			{
				int result = tlsHandshake(m_tls);

				if (1 == result)
				{
					m_tlsHandshake = false;

					if (NULL != m_incomingMsg)
					{
						ctxPush(m_incomingMsg);
						m_incomingMsg = NULL;
					}
				}
				else if (0 > result)
				{
					BX_TRACE("Disconnect %d - TLS handshake failed.", m_handle.idx);
					tlsHandshakeFailed();
					return false;
				}
				else
				{
					uint64_t now = bx::getHPCounter();
					if (now > m_tcpHandshakeTimeout)
					{
						BX_TRACE("Disconnect %d - TLS handshake timeout.", m_handle.idx);
						tlsHandshakeFailed();
						return false;
					}
				}
			}
#endif // BNET_CONFIG_TLS

			return true;
		}

		void tlsHandshakeFailed()
		{
			// When connection is incoming, user was never notified about it,
			// just drop it and recycle connection handle.
			const bool incoming = NULL != m_incomingMsg;

			disconnect();

			if (incoming)
			{
				Message* msg = msgAlloc(m_handle, 2, true);
				msg->data[0] = 0;
				msg->data[1] = Internal::Disconnect;
				ctxPush(msg);
			}
			else
			{
				ctxPush(m_handle, MessageId::ConnectFailed);
			}
		}

		bool send(const char* _data, uint32_t _len)
		{
			int bytes;
			uint32_t offset = 0;
			do
			{
#if BNET_CONFIG_TLS
				if (NULL != m_tls)
				{
					bytes = tlsSend(m_tls
						, &_data[offset]
						, _len
						);
				}
				else
#endif // BNET_CONFIG_TLS
				{
					bytes = ::send(m_socket
						, &_data[offset]
						, _len
						, 0
						);
				}

				if (0 > bytes)
				{
					if (-1 == bytes
					&&  !isWouldBlock() )
					{
						BX_TRACE("Disconnect %d - Send failed.", m_handle);
						disconnect(DisconnectReason::SendFailed);
						return false;
					}
				}
				else
				{
					_len -= bytes;
					offset += bytes;
				}

			} while (0 < _len);

			return true;
		}

		uint64_t m_tcpHandshakeTimeout;
		SOCKET m_socket;
		Handle m_handle;
		uint8_t* m_incomingBuffer;
		bx::RingBufferControl m_incoming;
		RecvRingBuffer m_recv;
		MessageQueue m_outgoing;
		Message* m_incomingMsg;
#if BNET_CONFIG_TLS
		TlsConnection* m_tls;
#endif // BNET_CONFIG_TLS

		int m_len;
		bool m_raw;
		bool m_tcpHandshake;
		bool m_tlsHandshake;
	};

	typedef FreeList<Connection> Connections;

	class ListenSocket
	{
	public:
		ListenSocket()
			: m_socket(INVALID_SOCKET)
			, m_handle(invalidHandle)
			, m_tlsCtx(NULL)
			, m_raw(false)
			, m_secure(false)
		{
		}

		~ListenSocket()
		{
			close();
		}

		void close()
		{
			if (INVALID_SOCKET != m_socket)
			{
				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;
			}

			if (NULL != m_tlsCtx)
			{
				tlsContextDestroy(m_tlsCtx);
				m_tlsCtx = NULL;
			}

			m_secure = false;
		}

		void listen(Handle _handle, uint32_t _ip, uint16_t _port, bool _raw, const char* _cert, const char* _key)
		{
			m_handle = _handle;
			m_raw    = _raw;
			m_secure = false;

			if (NULL != _cert
			||  NULL != _key)
			{
#if BNET_CONFIG_TLS
				if (NULL == _cert
				||  NULL == _key)
				{
					BX_TRACE("Secure listen requires both certificate and private key.");
					ctxPush(m_handle, MessageId::ListenFailed);
					return;
				}

				m_tlsCtx = tlsServerContextCreate(_cert, _key);

				if (NULL == m_tlsCtx)
				{
					BX_TRACE("Secure listen - TLS server context create failed.");
					ctxPush(m_handle, MessageId::ListenFailed);
					return;
				}

				m_secure = true;
#else
				BX_TRACE("Secure listen is not supported, TLS is disabled.");
				ctxPush(m_handle, MessageId::ListenFailed);
				return;
#endif // BNET_CONFIG_TLS
			}

			m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if (INVALID_SOCKET == m_socket)
			{
				BX_TRACE("Create socket failed.");
				ctxPush(m_handle, MessageId::ListenFailed);
				return;
			}

			setSockOpts(m_socket);

			m_addr.sin_family = AF_INET;
			m_addr.sin_addr.s_addr = htonl(_ip);
			m_addr.sin_port = htons(_port);

			if (SOCKET_ERROR == ::bind(m_socket, (sockaddr*)&m_addr, sizeof(m_addr) )
			||  SOCKET_ERROR == ::listen(m_socket, SOMAXCONN) )
			{
				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;

				BX_TRACE("Bind or listen socket failed.");
				ctxPush(m_handle, MessageId::ListenFailed);
				return;
			}

			setNonBlock(m_socket);
		}

		void update()
		{
			sockaddr_in addr;
			socklen_t len = sizeof(addr);
			SOCKET socket = ::accept(m_socket, (sockaddr*)&addr, &len);
			if (INVALID_SOCKET != socket)
			{
				uint32_t ip = ntohl(addr.sin_addr.s_addr);
				uint16_t port = ntohs(addr.sin_port);
				ctxAccept(m_handle, socket, ip, port, m_raw, m_tlsCtx);
			}
		}

	private:
		sockaddr_in m_addr;
		SOCKET m_socket;
		Handle m_handle;
		TlsContext* m_tlsCtx;
		bool m_raw;
		bool m_secure;
	};

	typedef FreeList<ListenSocket> ListenSockets;

	class Context
	{
	public:
		Context()
			: m_connections(NULL)
			, m_listenSockets(NULL)
			, m_tlsCtx(NULL)
		{
		}

		~Context()
		{
		}

		void init(uint16_t _maxConnections, uint16_t _maxListenSockets, const char* _certs[])
		{
			BX_UNUSED(_certs);
#if BNET_CONFIG_TLS
			m_tlsCtx = tlsContextCreate();
#endif // BNET_CONFIG_TLS

			_maxConnections = _maxConnections == 0 ? 1 : _maxConnections;

			m_connections = BX_NEW(g_allocator, Connections)(_maxConnections);

			if (0 != _maxListenSockets)
			{
				m_listenSockets = BX_NEW(g_allocator, ListenSockets)(_maxListenSockets);
			}
		}

		void shutdown()
		{
			for (Message* msg = m_incoming.pop(); NULL != msg; msg = m_incoming.pop() )
			{
				release(msg);
			}

			bx::deleteObject(g_allocator, m_connections);

			if (NULL != m_listenSockets)
			{
				bx::deleteObject(g_allocator, m_listenSockets);
			}

#if BNET_CONFIG_TLS
			if (NULL != m_tlsCtx)
			{
				tlsContextDestroy(m_tlsCtx);
				m_tlsCtx = NULL;
			}
#endif // BNET_CONFIG_TLS
		}

		Handle listen(uint32_t _ip, uint16_t _port, bool _raw, const char* _cert, const char* _key)
		{
			ListenSocket* listenSocket = m_listenSockets->create();
			if (NULL != listenSocket)
			{
				Handle handle = { m_listenSockets->getHandle(listenSocket) };
				listenSocket->listen(handle, _ip, _port, _raw, _cert, _key);
				return handle;
			}

			return invalidHandle;
		}

		void stop(Handle _handle)
		{
			ListenSocket* listenSocket = { m_listenSockets->getFromHandle(_handle.idx) };
			listenSocket->close();
			m_listenSockets->destroy(listenSocket);
		}

		Handle accept(Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx)
		{
			Connection* connection = m_connections->create();
			if (NULL != connection)
			{
				Handle handle = { m_connections->getHandle(connection) };

				if (!connection->accept(handle, _listenHandle, _socket, _ip, _port, _raw, _tlsCtx) )
				{
					m_connections->destroy(connection);
					return invalidHandle;
				}

				return handle;
			}

			::closesocket(_socket);

			return invalidHandle;
		}

		Handle connect(uint32_t _ip, uint16_t _port, bool _raw, bool _secure, const char* _hostname)
		{
			Connection* connection = m_connections->create();
			if (NULL != connection)
			{
				Handle handle = { m_connections->getHandle(connection) };
				connection->connect(handle, _ip, _port, _raw, _secure?m_tlsCtx:NULL, _hostname);
				return handle;
			}

			return invalidHandle;
		}

		void disconnect(Handle _handle, bool _finish)
		{
			BX_ASSERT(_handle.idx < m_connections->getMaxHandles(), "Invalid handle %d!", _handle.idx);

			Connection* connection = { m_connections->getFromHandle(_handle.idx) };
			if (_finish
			&&  connection->hasSocket() )
			{
				Message* msg = msgAlloc(_handle, 0, false, Internal::Disconnect);
				connection->send(msg);
			}
			else
			{
				BX_TRACE("Disconnect %d - Client closed connection.", _handle);
				connection->disconnect();

				Message* msg = msgAlloc(_handle, 2, true);
				msg->data[0] = 0;
				msg->data[1] = Internal::Disconnect;
				ctxPush(msg);
			}
		}

		void notify(Handle _handle, uint64_t _userData)
		{
			BX_ASSERT(_handle.idx == invalidHandle.idx // loopback
			      || _handle.idx < m_connections->getMaxHandles(), "Invalid handle %d!", _handle.idx);

			if (invalidHandle.idx != _handle.idx)
			{
				Message* msg = msgAlloc(_handle, sizeof(_userData), false, Internal::Notify);
				bx::memCopy(msg->data, &_userData, sizeof(_userData) );
				Connection* connection = m_connections->getFromHandle(_handle.idx);
				connection->send(msg);
			}
			else
			{
				// loopback
				Message* msg = msgAlloc(_handle, sizeof(_userData)+1, true);
				msg->data[0] = MessageId::Notify;
				bx::memCopy(&msg->data[1], &_userData, sizeof(_userData) );
				ctxPush(msg);
			}
		}

		void send(Message* _msg)
		{
			BX_ASSERT(_msg->handle.idx == invalidHandle.idx // loopback
			      || _msg->handle.idx < m_connections->getMaxHandles(), "Invalid handle %d!", _msg->handle.idx);

			if (invalidHandle.idx != _msg->handle.idx)
			{
				Connection* connection = m_connections->getFromHandle(_msg->handle.idx);
				connection->send(_msg);
			}
			else
			{
				// loopback
				push(_msg);
			}
		}

		Message* recv()
		{
			if (NULL != m_listenSockets)
			{
				for (uint16_t ii = 0, num = m_listenSockets->getNumHandles(); ii < num; ++ii)
				{
					ListenSocket* listenSocket = m_listenSockets->getFromHandleAt(ii);
					listenSocket->update();
				}
			}

			for (uint16_t ii = 0, num = m_connections->getNumHandles(); ii < num; ++ii)
			{
				Connection* connection = m_connections->getFromHandleAt(ii);
				connection->update();
			}

			Message* msg = m_incoming.pop();

			while (NULL != msg)
			{
				if (invalidHandle.idx == msg->handle.idx) // loopback
				{
					return msg;
				}

				Connection* connection = m_connections->getFromHandle(msg->handle.idx);

				uint8_t id = msg->data[0];
				if (0 == id
				&&  Internal::Disconnect == msg->data[1])
				{
					m_connections->destroy(connection);
				}
				else if (connection->hasSocket() || MessageId::UserDefined > id)
				{
					return msg;
				}

				release(msg);
				msg = m_incoming.pop();
			}

			return msg;
		}

		void push(Message* _msg)
		{
			m_incoming.push(_msg);
		}

	private:
		Connections* m_connections;
		ListenSockets* m_listenSockets;

		MessageQueue m_incoming;

		TlsContext* m_tlsCtx;
	};

	static Context s_ctx;

	Handle ctxAccept(Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx)
	{
		return s_ctx.accept(_listenHandle, _socket, _ip, _port, _raw, _tlsCtx);
	}

	void ctxPush(Handle _handle, MessageId::Enum _id)
	{
		Message* msg = msgAlloc(_handle, 1, true);
		msg->data[0] = uint8_t(_id);
		s_ctx.push(msg);
	}

	void ctxPush(Message* _msg)
	{
		s_ctx.push(_msg);
	}

	Message* msgAlloc(Handle _handle, uint16_t _size, bool _incoming, Internal::Enum _type)
	{
		uint16_t offset = _incoming ? 0 : 2;
		Message* msg = (Message*)bx::alloc(g_allocator, sizeof(Message) + offset + _size);
		msg->size = _size;
		msg->handle = _handle;
		uint8_t* data = (uint8_t*)msg + sizeof(Message);
		data[0] = uint8_t(_type);
		msg->data = data + offset;
		return msg;
	}

	void msgRelease(Message* _msg)
	{
		bx::free(g_allocator, _msg);
	}

	void init(uint16_t _maxConnections, uint16_t _maxListenSockets, const char* _certs[], bx::AllocatorI* _allocator)
	{
		if (NULL != _allocator)
		{
			g_allocator = _allocator;
		}

#if BX_PLATFORM_WINDOWS
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // BX_PLATFORM_WINDOWS

		s_ctx.init(_maxConnections, _maxListenSockets, _certs);
	}

	void shutdown()
	{
		s_ctx.shutdown();

#if BX_PLATFORM_WINDOWS
		WSACleanup();
#endif // BX_PLATFORM_WINDOWS
	}

	Handle listen(uint32_t _ip, uint16_t _port, bool _raw, const char* _cert, const char* _key)
	{
		return s_ctx.listen(_ip, _port, _raw, _cert, _key);
	}

	void stop(Handle _handle)
	{
		return s_ctx.stop(_handle);
	}

	Handle connect(uint32_t _ip, uint16_t _port, bool _raw, bool _secure, const char* _hostname)
	{
		return s_ctx.connect(_ip, _port, _raw, _secure, _secure ? _hostname : NULL);
	}

	Handle connect(const char* _host, uint16_t _port, bool _raw, bool _secure)
	{
		uint32_t ip = toIpv4(_host);
		if (0 == ip)
		{
			return invalidHandle;
		}

		return s_ctx.connect(ip, _port, _raw, _secure, _secure ? _host : NULL);
	}

	void disconnect(Handle _handle, bool _finish)
	{
		s_ctx.disconnect(_handle, _finish);
	}

	void notify(Handle _handle, uint64_t _userData)
	{
		s_ctx.notify(_handle, _userData);
	}

	OutgoingMessage* alloc(Handle _handle, uint16_t _size)
	{
		return msgAlloc(_handle, _size);
	}

	void release(IncomingMessage* _msg)
	{
		msgRelease(_msg);
	}

	void send(OutgoingMessage* _msg)
	{
		s_ctx.send(_msg);
	}

	IncomingMessage* recv()
	{
		return s_ctx.recv();
	}

	uint32_t toIpv4(const char* _addr)
	{
		uint32_t a0, a1, a2, a3;
		char dummy;
		if (4 == sscanf(_addr, "%d.%d.%d.%d%c", &a0, &a1, &a2, &a3, &dummy)
		&&  a0 <= 0xff
		&&  a1 <= 0xff
		&&  a2 <= 0xff
		&&  a3 <= 0xff)
		{
			return (a0<<24) | (a1<<16) | (a2<<8) | a3;
		}

		uint32_t ip = 0;
		struct addrinfo* result = NULL;
		struct addrinfo hints;
		bx::memSet(&hints, 0, sizeof(hints) );
		hints.ai_family = AF_UNSPEC;

		int res = getaddrinfo(_addr, NULL, &hints, &result);

		if (0 == res)
		{
			while (result)
			{
				sockaddr_in* addr = (sockaddr_in*)result->ai_addr;
				if (AF_INET == result->ai_family
				&&  INADDR_LOOPBACK != addr->sin_addr.s_addr)
				{
					ip = ntohl(addr->sin_addr.s_addr);
					break;
				}

				result = result->ai_next;
			}
		}

		if (NULL != result)
		{
			freeaddrinfo(result);
		}

		return ip;
	}

} // namespace bnet

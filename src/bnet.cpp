/*
 * Copyright 2010-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#	define _GNU_SOURCE // struct ucred, SO_PEERCRED
#endif // defined(__linux__) && !defined(_GNU_SOURCE)

#include "bnet_p.h"

#include <bx/endian.h>
#include <bx/allocators.h>
#include <bx/cpu.h>
#include <bx/hashmap.h>
#include <bx/readerwriter.h>

namespace bnet
{
	static bx::PointerAllocator* s_allocatorBlk = NULL;
	alignas(bx::PointerAllocator) static uint8_t s_allocatorBlkStorage[sizeof(bx::PointerAllocator)];
	bx::AllocatorBlkI* g_allocatorBlk = NULL;
	bx::AllocatorHandle g_allocatorHandle = { bx::kAllocatorHandleDefault };


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

	static void setSockOpts(SOCKET _socket, bool _tcp = true)
	{
		int result;

		int win = 256<<10;
		result = ::setsockopt(_socket, SOL_SOCKET, SO_RCVBUF, (char*)&win, sizeof(win) );
		result = ::setsockopt(_socket, SOL_SOCKET, SO_SNDBUF, (char*)&win, sizeof(win) );

		if (_tcp)
		{
			int noDelay = 1;
			result = ::setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, sizeof(noDelay) );
		}

		BX_UNUSED(result);
	}

#if BX_PLATFORM_WINDOWS
	struct LocalAddr
	{
		ADDRESS_FAMILY sun_family;
		char           sun_path[108];
	};
#else
	using LocalAddr = sockaddr_un;
#endif // BX_PLATFORM_WINDOWS

	static bool localAddress(LocalAddr& _out, const char* _path)
	{
		bx::memSet(&_out, 0, sizeof(_out) );
		_out.sun_family = AF_UNIX;

		const int32_t len = bx::strLen(_path);

		if (0 == len
		||  int32_t(sizeof(_out.sun_path) ) <= len)
		{
			return false;
		}

		bx::memCopy(_out.sun_path, _path, len);
		return true;
	}

	static void localUnlink(const char* _path)
	{
#if BX_PLATFORM_WINDOWS
		::_unlink(_path);
#else
		::unlink(_path);
#endif // BX_PLATFORM_WINDOWS
	}

	static bool localPeerTrusted(SOCKET _socket)
	{
#if BX_PLATFORM_OSX || BX_PLATFORM_IOS
		uid_t uid;
		gid_t gid;
		return true
			&& 0 == ::getpeereid(_socket, &uid, &gid)
			&& uid == ::geteuid()
			;
#elif BX_PLATFORM_LINUX || BX_PLATFORM_ANDROID
		ucred cred;
		socklen_t len = sizeof(cred);
		return true
			&& 0 == ::getsockopt(_socket, SOL_SOCKET, SO_PEERCRED, &cred, &len)
			&& cred.uid == ::geteuid()
			;
#else
		BX_UNUSED(_socket);
		return true;
#endif // BX_PLATFORM_*
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
			, m_incomingBuffer( (uint8_t*)bx::allocPtr(g_allocatorBlk, BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE) )
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
			, m_lost(false)
		{
		}

		~Connection()
		{
			disconnect();
			bx::freePtr(m_incomingBuffer);
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

		void connectLocal(Handle _handle, const char* _path, bool _raw)
		{
			init(_handle, _raw);

			LocalAddr addr;

			if (!localAddress(addr, _path) )
			{
				ctxPush(m_handle, MessageId::ConnectFailed);
				return;
			}

			m_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);

			if (INVALID_SOCKET == m_socket)
			{
				ctxPush(m_handle, MessageId::ConnectFailed);
				return;
			}

			setSockOpts(m_socket, false);
			setNonBlock(m_socket);

			if (0 != ::connect(m_socket, (const sockaddr*)&addr, sizeof(addr) )
			&&  !(isInProgress() || isWouldBlock() ) )
			{
				BX_TRACE("Connect %d - Local connect failed. %d", m_handle, getLastError() );

				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;

				ctxPush(m_handle, MessageId::ConnectFailed);
			}
		}

		SOCKET getSocket() const
		{
			return m_socket;
		}

		bool wantsWrite()
		{
			return false
				|| m_tcpHandshake
				|| m_tlsHandshake
				|| NULL != m_outgoing.peek()
				;
		}

		bool accept(Handle _handle, Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx, bool _local = false)
		{
			init(_handle, _raw);

			m_socket = _socket;

			setSockOpts(m_socket, !_local);
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
				m_lost = true;

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

		bool isLost() const
		{
			return m_lost;
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
			m_lost = false;
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
		bool m_lost;
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
			, m_local(false)
		{
			m_path[0] = '\0';
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

				if (m_local)
				{
					localUnlink(m_path);
				}
			}

			m_local   = false;
			m_path[0] = '\0';

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

		void listenLocal(Handle _handle, const char* _path, bool _raw)
		{
			m_handle = _handle;
			m_raw    = _raw;
			m_secure = false;

			LocalAddr addr;

			if (!localAddress(addr, _path) )
			{
				BX_TRACE("Local listen - path doesn't fit.");
				ctxPush(m_handle, MessageId::ListenFailed);
				return;
			}

			m_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);

			if (INVALID_SOCKET == m_socket)
			{
				BX_TRACE("Create local socket failed.");
				ctxPush(m_handle, MessageId::ListenFailed);
				return;
			}

			if (SOCKET_ERROR == ::bind(m_socket, (const sockaddr*)&addr, sizeof(addr) ) )
			{
				SOCKET probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
				const bool taken = true
					&& INVALID_SOCKET != probe
					&& 0 == ::connect(probe, (const sockaddr*)&addr, sizeof(addr) )
					;

				if (INVALID_SOCKET != probe)
				{
					::closesocket(probe);
				}

				if (!taken)
				{
					localUnlink(_path);
				}

				if (taken
				||  SOCKET_ERROR == ::bind(m_socket, (const sockaddr*)&addr, sizeof(addr) ) )
				{
					::closesocket(m_socket);
					m_socket = INVALID_SOCKET;

					BX_TRACE("Local listen - another process listens there, or bind failed.");
					ctxPush(m_handle, MessageId::ListenFailed);
					return;
				}
			}

			if (SOCKET_ERROR == ::listen(m_socket, SOMAXCONN) )
			{
				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;

				BX_TRACE("Bind or listen local socket failed.");
				ctxPush(m_handle, MessageId::ListenFailed);
				return;
			}

#if !BX_PLATFORM_WINDOWS
			::chmod(_path, S_IRUSR | S_IWUSR);
#endif // !BX_PLATFORM_WINDOWS

			bx::strCopy(m_path, sizeof(m_path), _path);
			m_local = true;

			setNonBlock(m_socket);
		}

		void update()
		{
			if (m_local)
			{
				SOCKET socket = ::accept(m_socket, NULL, NULL);

				if (INVALID_SOCKET != socket)
				{
					if (!localPeerTrusted(socket) )
					{
						BX_TRACE("Local accept - another user's process, refused.");
						::closesocket(socket);
						return;
					}

					ctxAccept(m_handle, socket, 0, 0, m_raw, NULL, true);
				}

				return;
			}

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

		SOCKET getSocket() const
		{
			return m_socket;
		}

	private:
		sockaddr_in m_addr;
		SOCKET m_socket;
		Handle m_handle;
		TlsContext* m_tlsCtx;
		char m_path[sizeof(LocalAddr::sun_path)];
		bool m_raw;
		bool m_secure;
		bool m_local;
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
			m_wake[0] = INVALID_SOCKET;
			m_wake[1] = INVALID_SOCKET;
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

			m_connections = BX_NEW_PTR(g_allocatorBlk, Connections)(_maxConnections);

			if (0 != _maxListenSockets)
			{
				m_listenSockets = BX_NEW_PTR(g_allocatorBlk, ListenSockets)(_maxListenSockets);
			}

			wakeCreate();
		}

		void wakeCreate()
		{
#if BX_PLATFORM_WINDOWS
			SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			sockaddr_in addr;
			bx::memSet(&addr, 0, sizeof(addr) );
			addr.sin_family      = AF_INET;
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			socklen_t len = sizeof(addr);

			if (INVALID_SOCKET != listener
			&&  0 == ::bind(listener, (sockaddr*)&addr, sizeof(addr) )
			&&  0 == ::listen(listener, 1)
			&&  0 == ::getsockname(listener, (sockaddr*)&addr, &len) )
			{
				m_wake[1] = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

				if (INVALID_SOCKET != m_wake[1]
				&&  0 == ::connect(m_wake[1], (sockaddr*)&addr, sizeof(addr) ) )
				{
					m_wake[0] = ::accept(listener, NULL, NULL);
				}
			}

			if (INVALID_SOCKET != listener)
			{
				::closesocket(listener);
			}
#else
			int fds[2];

			if (0 == ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) )
			{
				m_wake[0] = fds[0];
				m_wake[1] = fds[1];
			}
#endif // BX_PLATFORM_WINDOWS

			for (SOCKET socket : m_wake)
			{
				if (INVALID_SOCKET != socket)
				{
					setNonBlock(socket);
				}
			}
		}

		void wake()
		{
			if (INVALID_SOCKET != m_wake[1])
			{
				const char byte = 0;
				const int result = ::send(m_wake[1], &byte, 1, 0);
				BX_UNUSED(result);
			}
		}

		void wait(int32_t _msecs, bx::Mutex& _mutex)
		{
			static constexpr uint32_t kMaxPoll = 1024;

#if BX_PLATFORM_WINDOWS
			using PollFd = WSAPOLLFD;
#else
			using PollFd = pollfd;
#endif // BX_PLATFORM_WINDOWS

			PollFd fds[kMaxPoll];
			uint32_t num = 0;

			{
				bx::MutexScope lock(_mutex);

				if (NULL != m_incoming.peek() )
				{
					return;
				}

				const auto add = [&](SOCKET _socket, short _events)
				{
					if (INVALID_SOCKET != _socket
					&&  num < kMaxPoll)
					{
						fds[num].fd      = _socket;
						fds[num].events  = _events;
						fds[num].revents = 0;
						++num;
					}
				};

				add(m_wake[0], POLLIN);

				if (NULL != m_listenSockets)
				{
					for (uint16_t handle = m_listenSockets->getFirstHandle(); bx::kInvalidHandle != handle; handle = m_listenSockets->getNextHandle(handle) )
					{
						add(m_listenSockets->getFromHandle(handle)->getSocket(), POLLIN);
					}
				}

				for (uint16_t handle = m_connections->getFirstHandle(); bx::kInvalidHandle != handle; handle = m_connections->getNextHandle(handle) )
				{
					Connection* connection = m_connections->getFromHandle(handle);
					add(connection->getSocket(), short(POLLIN | (connection->wantsWrite() ? POLLOUT : 0) ) );
				}
			}

#if BX_PLATFORM_WINDOWS
			::WSAPoll(fds, ULONG(num), _msecs);
#else
			::poll(fds, nfds_t(num), _msecs);
#endif // BX_PLATFORM_WINDOWS

			if (INVALID_SOCKET != m_wake[0])
			{
				char buffer[64];

				while (0 < ::recv(m_wake[0], buffer, sizeof(buffer), 0) )
				{
				}
			}
		}

		void shutdown()
		{
			for (Message* msg = m_incoming.pop(); NULL != msg; msg = m_incoming.pop() )
			{
				release(msg);
			}

			for (SOCKET& socket : m_wake)
			{
				if (INVALID_SOCKET != socket)
				{
					::closesocket(socket);
					socket = INVALID_SOCKET;
				}
			}

			bx::deletePtr(m_connections);
			m_connections = NULL;

			if (NULL != m_listenSockets)
			{
				bx::deletePtr(m_listenSockets);
				m_listenSockets = NULL;
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
			ListenSocket* listenSocket = NULL != m_listenSockets ? m_listenSockets->create() : NULL;
			if (NULL != listenSocket)
			{
				Handle handle = { m_listenSockets->getHandle(listenSocket) };
				listenSocket->listen(handle, _ip, _port, _raw, _cert, _key);
				return handle;
			}

			return invalidHandle;
		}

		Handle listenLocal(const char* _path, bool _raw)
		{
			ListenSocket* listenSocket = NULL != m_listenSockets ? m_listenSockets->create() : NULL;
			if (NULL != listenSocket)
			{
				Handle handle = { m_listenSockets->getHandle(listenSocket) };
				listenSocket->listenLocal(handle, _path, _raw);
				return handle;
			}

			return invalidHandle;
		}

		Handle connectLocal(const char* _path, bool _raw)
		{
			Connection* connection = m_connections->create();
			if (NULL != connection)
			{
				Handle handle = { m_connections->getHandle(connection) };
				connection->connectLocal(handle, _path, _raw);
				return handle;
			}

			return invalidHandle;
		}

		void stop(Handle _handle)
		{
			if (NULL == m_listenSockets)
			{
				return;
			}

			ListenSocket* listenSocket = { m_listenSockets->getFromHandle(_handle.idx) };
			listenSocket->close();
			m_listenSockets->destroy(listenSocket);
		}

		Handle accept(Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx, bool _local)
		{
			Connection* connection = m_connections->create();
			if (NULL != connection)
			{
				Handle handle = { m_connections->getHandle(connection) };

				if (!connection->accept(handle, _listenHandle, _socket, _ip, _port, _raw, _tlsCtx, _local) )
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
				for (uint16_t handle = m_listenSockets->getFirstHandle(); bx::kInvalidHandle != handle; handle = m_listenSockets->getNextHandle(handle) )
				{
					ListenSocket* listenSocket = m_listenSockets->getFromHandle(handle);
					listenSocket->update();
				}
			}

			for (uint16_t handle = m_connections->getFirstHandle(); bx::kInvalidHandle != handle; handle = m_connections->getNextHandle(handle) )
			{
				Connection* connection = m_connections->getFromHandle(handle);
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
				else if (connection->hasSocket() || connection->isLost() || MessageId::UserDefined > id)
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
		SOCKET m_wake[2];
	};

	static Context   s_ctx;
	static bx::Mutex s_mutex;

	Handle ctxAccept(Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx, bool _local)
	{
		return s_ctx.accept(_listenHandle, _socket, _ip, _port, _raw, _tlsCtx, _local);
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

	static constexpr uint32_t kMessageStackSkip      = 2;
	static constexpr uint32_t kMessageStackMaxFrames = 8;
	static constexpr uint32_t kMessageStackMaxText   = 2048;

	struct MessageStack
	{
		uintptr_t frames[kMessageStackMaxFrames];
		uint32_t  num;
		uint32_t  size;
	};

	using MessageStackMap = bx::HashMapT<uintptr_t, MessageStack>;

	static int32_t          s_numMessages   = 0;
	static MessageStackMap* s_messageStacks = NULL;
	static bx::Mutex        s_messageMutex;

	static void msgTrack(const Message* _msg)
	{
		bx::atomicFetchAndAdd(&s_numMessages, 1);

		if (BX_ENABLED(BX_CONFIG_DEBUG) )
		{
			MessageStack stack;
			stack.num  = bx::getCallStackFast(kMessageStackSkip, kMessageStackMaxFrames, stack.frames);
			stack.size = _msg->size;

			bx::MutexScope lock(s_messageMutex);

			if (NULL == s_messageStacks)
			{
				s_messageStacks = BX_NEW_PTR(g_allocatorBlk, MessageStackMap)(g_allocatorHandle);
			}

			s_messageStacks->add(uintptr_t(_msg), stack);
		}
	}

	static void msgUntrack(const Message* _msg)
	{
		bx::atomicFetchAndSub(&s_numMessages, 1);

		if (BX_ENABLED(BX_CONFIG_DEBUG) )
		{
			bx::MutexScope lock(s_messageMutex);

			if (NULL != s_messageStacks)
			{
				s_messageStacks->tryRemove(uintptr_t(_msg) );
			}
		}
	}

	static void msgCheckLeaks()
	{
		const int32_t numMessages = bx::atomicExchange(&s_numMessages, 0);

		if (BX_ENABLED(BX_CONFIG_DEBUG)
		&&  NULL != s_messageStacks)
		{
			if (!s_messageStacks->isEmpty() )
			{
				BX_TRACE("LEAK: Message %d", s_messageStacks->getCount() );

				uint32_t ii = 0;
				for (MessageStackMap::Iterator it = s_messageStacks->begin(), itEnd = s_messageStacks->end(); it != itEnd; ++it, ++ii)
				{
					const MessageStack& stack = it.getValue();

					char text[kMessageStackMaxText];
					bx::StaticMemoryBlockWriter writer(text, sizeof(text)-1);
					bx::ErrorIgnore err;
					const int32_t len = bx::writeCallstack(&writer, stack.frames, stack.num, &err);
					text[len] = '\0';

					BX_TRACE("\t%3d: %p, size %d\n%s"
						, ii
						, (const void*)it.getKey()
						, stack.size
						, text
						);
				}
			}

			bx::deletePtr(s_messageStacks);
			s_messageStacks = NULL;
		}

		BX_ASSERT(0 == numMessages
			, "%d bnet::Message(s) from bnet::alloc or bnet::recv were never passed to bnet::send or bnet::release."
			, numMessages
			);
		BX_UNUSED(numMessages);
	}

	Message* msgAlloc(Handle _handle, uint16_t _size, bool _incoming, Internal::Enum _type)
	{
		uint16_t offset = _incoming ? 0 : 2;
		Message* msg = (Message*)bx::allocPtr(g_allocatorBlk, sizeof(Message) + offset + _size);
		msg->size = _size;
		msg->handle = _handle;
		uint8_t* data = (uint8_t*)msg + sizeof(Message);
		data[0] = uint8_t(_type);
		msg->data = data + offset;

		msgTrack(msg);

		return msg;
	}

	void msgRelease(Message* _msg)
	{
		msgUntrack(_msg);
		bx::freePtr(_msg);
	}

	void init(uint16_t _maxConnections, uint16_t _maxListenSockets, const char* _certs[], bx::AllocatorI* _allocator)
	{
		if (NULL != _allocator)
		{
			g_allocatorBlk =
				s_allocatorBlk = BX_PLACEMENT_NEW(s_allocatorBlkStorage, bx::PointerAllocator)(_allocator);
		}
		else
		{
			g_allocatorBlk = bx::getDefaultAllocatorBlk();
		}

		g_allocatorHandle = g_allocatorBlk->getHandle();

#if BX_PLATFORM_WINDOWS
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif // BX_PLATFORM_WINDOWS

		bx::MutexScope lock(s_mutex);
		s_ctx.init(_maxConnections, _maxListenSockets, _certs);
	}

	void shutdown()
	{
		{
			bx::MutexScope lock(s_mutex);
			s_ctx.shutdown();
		}

#if BX_PLATFORM_WINDOWS
		WSACleanup();
#endif // BX_PLATFORM_WINDOWS

		msgCheckLeaks();

		if (NULL != s_allocatorBlk)
		{
			s_allocatorBlk->~PointerAllocator();
			s_allocatorBlk = NULL;
		}

		g_allocatorBlk = NULL;
		g_allocatorHandle = { bx::kAllocatorHandleDefault };
	}

	Handle listen(uint32_t _ip, uint16_t _port, bool _raw, const char* _cert, const char* _key)
	{
		bx::MutexScope lock(s_mutex);
		const Handle handle = s_ctx.listen(_ip, _port, _raw, _cert, _key);
		s_ctx.wake();
		return handle;
	}

	Handle listenLocal(const char* _path, bool _raw)
	{
		bx::MutexScope lock(s_mutex);
		const Handle handle = s_ctx.listenLocal(_path, _raw);
		s_ctx.wake();
		return handle;
	}

	void stop(Handle _handle)
	{
		bx::MutexScope lock(s_mutex);
		s_ctx.stop(_handle);
		s_ctx.wake();
	}

	Handle connect(uint32_t _ip, uint16_t _port, bool _raw, bool _secure, const char* _hostname)
	{
		bx::MutexScope lock(s_mutex);
		const Handle handle = s_ctx.connect(_ip, _port, _raw, _secure, _secure ? _hostname : NULL);
		s_ctx.wake();
		return handle;
	}

	Handle connectLocal(const char* _path, bool _raw)
	{
		bx::MutexScope lock(s_mutex);
		const Handle handle = s_ctx.connectLocal(_path, _raw);
		s_ctx.wake();
		return handle;
	}

	Handle connect(const char* _host, uint16_t _port, bool _raw, bool _secure)
	{
		uint32_t ip = toIpv4(_host);
		if (0 == ip)
		{
			return invalidHandle;
		}

		bx::MutexScope lock(s_mutex);
		const Handle handle = s_ctx.connect(ip, _port, _raw, _secure, _secure ? _host : NULL);
		s_ctx.wake();
		return handle;
	}

	void disconnect(Handle _handle, bool _finish)
	{
		bx::MutexScope lock(s_mutex);
		s_ctx.disconnect(_handle, _finish);
		s_ctx.wake();
	}

	void notify(Handle _handle, uint64_t _userData)
	{
		bx::MutexScope lock(s_mutex);
		s_ctx.notify(_handle, _userData);
		s_ctx.wake();
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
		bx::MutexScope lock(s_mutex);
		s_ctx.send(_msg);
		s_ctx.wake();
	}

	IncomingMessage* recv()
	{
		bx::MutexScope lock(s_mutex);
		return s_ctx.recv();
	}

	void wait(int32_t _msecs)
	{
		s_ctx.wait(_msecs, s_mutex);
	}

	void wake()
	{
		s_ctx.wake();
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

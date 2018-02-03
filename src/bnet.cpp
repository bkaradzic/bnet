/*
 * Copyright 2010-2018 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet#license-bsd-2-clause
 */

#include "bnet_p.h"

#include <bx/endian.h>
#include <bx/allocator.h>

namespace bnet
{
	static bx::DefaultAllocator s_allocatorStub;
	bx::AllocatorI* g_allocator = &s_allocatorStub;

#if BNET_CONFIG_OPENSSL && BNET_CONFIG_DEBUG

	static void getSslErrorInfo()
	{
		BIO* bio = BIO_new(BIO_s_mem());
		ERR_print_errors(bio);
		BUF_MEM *bptr;
		BIO_get_mem_ptr(bio, &bptr);
		BX_TRACE("OpenSSL Error: %.*s", bptr->length, bptr->data);
		BIO_free(bio);
	}

#	define TRACE_SSL_ERROR() getSslErrorInfo()
#else
#	define TRACE_SSL_ERROR()
#endif // BNET_CONFIG_OPENSSL && BNET_CONFIG_DEBUG

	int getLastError()
	{
#if BX_PLATFORM_WINDOWS
		return WSAGetLastError();
#elif  BX_PLATFORM_LINUX \
	|| BX_PLATFORM_ANDROID \
	|| BX_PLATFORM_OSX \
	|| BX_PLATFORM_IOS
		return errno;
#else
		return 0;
#endif // BX_PLATFORM_
	}

#if BNET_CONFIG_OPENSSL
#else
	static int sslDummyContext;
#endif

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

	class Connection
	{
	public:
		Connection()
			: m_socket(INVALID_SOCKET)
			, m_handle(invalidHandle)
			, m_incomingBuffer( (uint8_t*)BX_ALLOC(g_allocator, BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE) )
			, m_incoming(BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE)
			, m_recv(m_incoming, (char*)m_incomingBuffer)
#if BNET_CONFIG_OPENSSL
			, m_ssl(NULL)
#endif // BNET_CONFIG_OPENSSL
			, m_len(-1)
			, m_raw(false)
			, m_tcpHandshake(true)
			, m_sslHandshake(false)
		{
		}

		~Connection()
		{
			BX_FREE(g_allocator, m_incomingBuffer);
		}

		void connect(Handle _handle, uint32_t _ip, uint16_t _port, bool _raw, SSL_CTX* _sslCtx)
		{
			init(_handle, _raw);

			m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (INVALID_SOCKET == m_socket)
			{
				ctxPush(m_handle, MessageId::ConnectFailed);
				return;
			}

			setSockOpts(m_socket);

			const bool ssl = _sslCtx != NULL;
			int err = connectsocket(m_socket, _ip, _port, ssl);

			if (0 != err
			&&  !(isInProgress() || isWouldBlock() ) )
			{
				BX_TRACE("Connect %d - Connect failed. %d", m_handle, getLastError() );

				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;

				ctxPush(m_handle, MessageId::ConnectFailed);
				return;
			}

			setNonBlock(m_socket);

#if BNET_CONFIG_OPENSSL
			if (ssl)
			{
				m_sslHandshake = true;
				m_ssl = SSL_new(_sslCtx);
				SSL_set_fd(m_ssl, (int)m_socket);
				SSL_set_connect_state(m_ssl);
				SSL_write(m_ssl, NULL, 0);
			}
#else
			BX_UNUSED(_sslCtx);
#endif // BNET_CONFIG_OPENSSL
		}

		void accept(Handle _handle, Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, SSL_CTX* _sslCtx, X509* _cert, EVP_PKEY* _key)
		{
			init(_handle, _raw);

			m_socket = _socket;
			Message* msg = msgAlloc(m_handle, 9, true);
			msg->data[0] = MessageId::IncomingConnection;
			*( (uint16_t*)&msg->data[1]) = _listenHandle.idx;
			*( (uint32_t*)&msg->data[3]) = _ip;
			*( (uint16_t*)&msg->data[7]) = _port;
			ctxPush(msg);

#if BNET_CONFIG_OPENSSL
			if (NULL != _sslCtx)
			{
				m_sslHandshake = true;
				m_ssl = SSL_new(_sslCtx);
				int result;
				result = SSL_use_certificate(m_ssl, _cert);
				result = SSL_use_PrivateKey(m_ssl, _key);
				result = SSL_set_fd(m_ssl, (int)m_socket);
				BX_UNUSED(result);
				SSL_set_accept_state(m_ssl);
				SSL_read(m_ssl, NULL, 0);
			}
#else
			BX_UNUSED(_sslCtx);
			BX_UNUSED(_cert);
			BX_UNUSED(_key);
#endif // BNET_CONFIG_OPENSSL
		}

		void disconnect(DisconnectReason::Enum _reason = DisconnectReason::None)
		{
#if BNET_CONFIG_OPENSSL
			if (m_ssl)
			{
				SSL_shutdown(m_ssl);
				SSL_free(m_ssl);
				m_ssl = NULL;
			}
#endif // BNET_CONFIG_OPENSSL

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
			BX_CHECK(m_raw || _msg->data[0] >= MessageId::UserDefined, "Sending message with MessageId below UserDefined is not allowed!");
			if (INVALID_SOCKET != m_socket)
			{
				m_outgoing.push(_msg);
				update();
			}
			else
			{
				msgRelease(_msg);
			}
		}

		void update()
		{
			if (INVALID_SOCKET != m_socket)
			{
				updateSocket();

				if (!m_tcpHandshake
				&&  !m_sslHandshake)
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
			m_sslHandshake = false;
			m_tcpHandshakeTimeout = bx::getHPCounter() + bx::getHPFrequency()*BNET_CONFIG_CONNECT_TIMEOUT_SECONDS;
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
				uint16_t available = uint16_t(bx::uint32_min(m_incoming.available(), maxMessageSize-1) );

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
				uint32_t available = bx::uint32_min(m_incoming.available(), maxMessageSize);

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

					available = bx::uint32_min(m_incoming.available(), maxMessageSize);
				}
			}
		}

		void updateSocket()
		{
			if (updateTcpHandshake()
			&&  updateSslHandshake() )
			{
				int bytes;

#if BNET_CONFIG_OPENSSL
				if (NULL != m_ssl)
				{
					bytes = m_recv.recv(m_ssl);
				}
				else
#endif // BNET_CONFIG_OPENSSL
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
						TRACE_SSL_ERROR();
						BX_TRACE("Disconnect %d - Receive failed. %d", m_handle, getLastError() );
						disconnect(DisconnectReason::RecvFailed);
						return;
					}
				}

				if (!m_sslHandshake)
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

			BX_CHECK(false, "You shoud not be here!");
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

			m_tcpHandshake = !issocketready(m_socket);
			return !m_tcpHandshake;
		}

		bool updateSslHandshake()
		{
#if BNET_CONFIG_OPENSSL
			if (NULL != m_ssl
			&&  m_sslHandshake)
			{
				int err = SSL_do_handshake(m_ssl);

				if (1 == err)
				{
					m_sslHandshake = false;
#	if BNET_CONFIG_DEBUG
					X509* cert = SSL_get_peer_certificate(m_ssl);
					BX_TRACE("Server certificate:");

					char* temp;
					temp = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
					BX_TRACE("\t subject: %s", temp);
					OPENSSL_free(temp);

					temp = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
					BX_TRACE("\t issuer: %s", temp);
					OPENSSL_free(temp);

					X509_free(cert);
#	endif // BNET_CONFIG_DEBUG

					long result = SSL_get_verify_result(m_ssl);
					if (X509_V_OK != result)
					{
						BX_TRACE("Disconnect %d - SSL verify failed %d.", m_handle, result);
						ctxPush(m_handle, MessageId::ConnectFailed);
						disconnect();
						return false;
					}

					BX_TRACE("SSL connection using %s", SSL_get_cipher(m_ssl) );
				}
				else
				{
					int sslError = SSL_get_error(m_ssl, err);
					switch (sslError)
					{
					case SSL_ERROR_WANT_READ:
						SSL_read(m_ssl, NULL, 0);
						break;

					case SSL_ERROR_WANT_WRITE:
						SSL_write(m_ssl, NULL, 0);
						break;

					default:
						TRACE_SSL_ERROR();
						break;
					}
				}
			}
#endif // BNET_CONFIG_OPENSSL

			return true;
		}

		bool send(const char* _data, uint32_t _len)
		{
			int bytes;
			uint32_t offset = 0;
			do
			{
#if BNET_CONFIG_OPENSSL
				if (NULL != m_ssl)
				{
					bytes = SSL_write(m_ssl
						, &_data[offset]
						, _len
						);
				}
				else
#endif // BNET_CONFIG_OPENSSL
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
#if BNET_CONFIG_OPENSSL
		SSL* m_ssl;
#endif // BNET_CONFIG_OPENSSL

		int m_len;
		bool m_raw;
		bool m_tcpHandshake;
		bool m_sslHandshake;
	};

	typedef FreeList<Connection> Connections;

	class ListenSocket
	{
	public:
		ListenSocket()
			: m_socket(INVALID_SOCKET)
			, m_handle(invalidHandle)
			, m_raw(false)
			, m_secure(false)
			, m_cert(NULL)
			, m_key(NULL)
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

#if BNET_CONFIG_OPENSSL
			if (NULL != m_cert)
			{
				X509_free(m_cert);
				m_cert = NULL;
			}

			if (NULL != m_key)
			{
				EVP_PKEY_free(m_key);
				m_key = NULL;
			}
#endif // BNET_CONFIG_OPENSSL
		}

		void listen(Handle _handle, uint32_t _ip, uint16_t _port, bool _raw, const char* _cert, const char* _key)
		{
			m_handle = _handle;
			m_raw = _raw;

#if BNET_CONFIG_OPENSSL
			if (NULL != _cert)
			{
				BIO* mem = BIO_new_mem_buf(const_cast<char*>(_cert), -1);
				m_cert = PEM_read_bio_X509(mem, NULL, NULL, NULL);
				BIO_free(mem);
			}

			if (NULL != _key)
			{
				BIO* mem = BIO_new_mem_buf(const_cast<char*>(_key), -1);
				m_key = PEM_read_bio_PrivateKey(mem, NULL, NULL, NULL);
				BIO_free(mem);
			}

			m_secure = NULL != m_key && NULL != m_cert;
#endif // BNET_CONFIG_OPENSSL

			if (!m_secure
			&&  (NULL != _cert || NULL != _key) )
			{
#if BNET_CONFIG_OPENSSL
				BX_TRACE("Certificate of key is not set correctly.");
#else
				BX_TRACE("BNET_CONFIG_OPENSSL is not enabled.");
#endif // BNET_CONFIG_OPENSSL
				ctxPush(m_handle, MessageId::ListenFailed);
				return;
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
				ctxAccept(m_handle, socket, ip, port, m_raw, m_cert, m_key);
			}
		}

	private:
		sockaddr_in m_addr;
		SOCKET m_socket;
		Handle m_handle;
		bool m_raw;
		bool m_secure;
		X509* m_cert;
		EVP_PKEY* m_key;
	};

	typedef FreeList<ListenSocket> ListenSockets;

	class Context
	{
	public:
		Context()
			: m_connections(NULL)
			, m_listenSockets(NULL)
			, m_sslCtx(NULL)
			, m_sslCtxServer(NULL)
		{
		}

		~Context()
		{
		}

		void init(uint16_t _maxConnections, uint16_t _maxListenSockets, const char* _certs[])
		{
#if BNET_CONFIG_OPENSSL
			CRYPTO_get_mem_functions(&m_sslMalloc, &m_sslRealloc, &m_sslFree);
			CRYPTO_set_mem_functions(sslMalloc, sslRealloc, sslFree);
			SSL_library_init();
#	if BNET_CONFIG_DEBUG
			SSL_load_error_strings();
#	endif // BNET_CONFIG_DEBUG
			m_sslCtx = SSL_CTX_new(SSLv23_client_method() );
			SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_NONE, NULL);
			if (NULL != _certs)
			{
				X509_STORE* store = SSL_CTX_get_cert_store(m_sslCtx);
				for (const char** cert = _certs; NULL != *cert; ++cert )
				{
					BIO* mem = BIO_new_mem_buf(const_cast<char*>(*cert), -1);
					X509* x509 = PEM_read_bio_X509(mem, NULL, NULL, NULL);
					X509_STORE_add_cert(store, x509);
					X509_free(x509);
					BIO_free(mem);
				}
			}

			if (_maxListenSockets)
			{
				m_sslCtxServer = SSL_CTX_new(SSLv23_server_method());
			}
#else
			m_sslCtx = &sslDummyContext;
			m_sslCtxServer = &sslDummyContext;
			BX_UNUSED(_certs);
#endif // BNET_CONFIG_OPENSSL

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

			BX_DELETE(g_allocator, m_connections);

			if (NULL != m_listenSockets)
			{
				BX_DELETE(g_allocator, m_listenSockets);
			}

#if BNET_CONFIG_OPENSSL
			if (NULL != m_sslCtx)
			{
				SSL_CTX_free(m_sslCtx);
			}
			m_sslCtx = NULL;

			if (NULL != m_sslCtxServer)
			{
				SSL_CTX_free(m_sslCtxServer);
			}
			m_sslCtxServer = NULL;
			CRYPTO_set_mem_functions(m_sslMalloc, m_sslRealloc, m_sslFree);
#endif // BNET_CONFIG_OPENSSL
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

		Handle accept(Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, X509* _cert, EVP_PKEY* _key)
		{
			Connection* connection = m_connections->create();
			if (NULL != connection)
			{
				Handle handle = { m_connections->getHandle(connection) };
				bool secure = NULL != _cert && NULL != _key;
				connection->accept(handle, _listenHandle, _socket, _ip, _port, _raw, secure?m_sslCtxServer:NULL, _cert, _key);
				return handle;
			}

			return invalidHandle;
		}

		Handle connect(uint32_t _ip, uint16_t _port, bool _raw, bool _secure)
		{
			Connection* connection = m_connections->create();
			if (NULL != connection)
			{
				Handle handle = { m_connections->getHandle(connection) };
				connection->connect(handle, _ip, _port, _raw, _secure?m_sslCtx:NULL);
				return handle;
			}

			return invalidHandle;
		}

		void disconnect(Handle _handle, bool _finish)
		{
			BX_CHECK(_handle.idx < m_connections->getMaxHandles(), "Invalid handle %d!", _handle.idx);

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
			BX_CHECK(_handle.idx == invalidHandle.idx // loopback
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
			BX_CHECK(_msg->handle.idx == invalidHandle.idx // loopback
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

#if BNET_CONFIG_OPENSSL
		static void* sslMalloc(size_t _size)
		{
			return BX_ALLOC(g_allocator, _size);
		}

		static void* sslRealloc(void* _ptr, size_t _size)
		{
			return BX_REALLOC(g_allocator, _ptr, _size);
		}

		static void sslFree(void* _ptr)
		{
			return BX_FREE(g_allocator, _ptr);
		}

		typedef void* (*MallocFn)(size_t _size);
		MallocFn m_sslMalloc;

		typedef void* (*ReallocFn)(void* _ptr, size_t _size);
		ReallocFn m_sslRealloc;

		typedef void (*FreeFn)(void* _ptr);
		FreeFn m_sslFree;
#endif // BNET_CONFIG_OPENSSL

		SSL_CTX* m_sslCtx;
		SSL_CTX* m_sslCtxServer;
	};

	static Context s_ctx;

	Handle ctxAccept(Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, X509* _cert, EVP_PKEY* _key)
	{
		return s_ctx.accept(_listenHandle, _socket, _ip, _port, _raw, _cert, _key);
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
		Message* msg = (Message*)BX_ALLOC(g_allocator, sizeof(Message) + offset + _size);
		msg->size = _size;
		msg->handle = _handle;
		uint8_t* data = (uint8_t*)msg + sizeof(Message);
		data[0] = uint8_t(_type);
		msg->data = data + offset;
		return msg;
	}

	void msgRelease(Message* _msg)
	{
		BX_FREE(g_allocator, _msg);
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

	Handle connect(uint32_t _ip, uint16_t _port, bool _raw, bool _secure)
	{
		return s_ctx.connect(_ip, _port, _raw, _secure);
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

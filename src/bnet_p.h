/*
 * Copyright 2010-2012 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef __BNET_P_H__
#define __BNET_P_H__

#include "bnet.h"

#ifndef BNET_CONFIG_DEBUG
#	define BNET_CONFIG_DEBUG 0
#endif // BNET_CONFIG_DEBUG

extern void dbgPrintf(const char* _format, ...);
extern void dbgPrintfData(const void* _data, uint32_t _size, const char* _format, ...);

#if BNET_CONFIG_DEBUG
#	define BX_TRACE(_format, ...) \
				do { \
					dbgPrintf(BX_FILE_LINE_LITERAL "BNET " _format "\n", ##__VA_ARGS__); \
				} while(0)

#	define BX_CHECK(_condition, _format, ...) \
				do { \
					if (!(_condition) ) \
					{ \
						BX_TRACE(BX_FILE_LINE_LITERAL _format, ##__VA_ARGS__); \
						bx::debugBreak(); \
					} \
				} while(0)
#endif // 0

#define BX_NAMESPACE 1
#include <bx/bx.h>

#ifndef BNET_CONFIG_OPENSSL
#	define BNET_CONFIG_OPENSSL (BX_PLATFORM_WINDOWS && BX_COMPILER_MSVC) || BX_PLATFORM_ANDROID || BX_PLATFORM_LINUX
#endif // BNET_CONFIG_OPENSSL

#ifndef BNET_CONFIG_DEBUG
#	define BNET_CONFIG_DEBUG 0
#endif // BNET_CONFIG_DEBUG

#ifndef BNET_CONFIG_CONNECT_TIMEOUT_SECONDS
#	define BNET_CONFIG_CONNECT_TIMEOUT_SECONDS 5
#endif // BNET_CONFIG_CONNECT_TIMEOUT_SECONDS

#ifndef BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE
#	define BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE (64<<10)
#endif // BNET_CONFIG_MAX_INCOMING_BUFFER_SIZE

#if BX_PLATFORM_WINDOWS || BX_PLATFORM_XBOX360
#	if BX_PLATFORM_WINDOWS
#		if !defined(_WIN32_WINNT)
#			define _WIN32_WINNT 0x0501
#		endif
#		include <winsock2.h>
#		include <ws2tcpip.h>
#	elif BX_PLATFORM_XBOX360
#		include <xtl.h>
#	endif
#	define socklen_t int32_t
#	define EWOULDBLOCK WSAEWOULDBLOCK
#	define EINPROGRESS WSAEINPROGRESS
#	include "inet_socket.h"
#elif BX_PLATFORM_LINUX || BX_PLATFORM_ANDROID || BX_PLATFORM_OSX || BX_PLATFORM_IOS
#	include <memory.h>
#	include <errno.h> // errno
#	include <fcntl.h>
#	include <netdb.h>
#	include <unistd.h>
#	include <sys/socket.h>
#	include <sys/time.h> // gettimeofday
#	include <arpa/inet.h> // inet_addr
#	include <netinet/in.h>
#	include <netinet/tcp.h>
	typedef int SOCKET;
	typedef linger LINGER;
	typedef hostent HOSTENT;
	typedef in_addr IN_ADDR;
	
#	define SOCKET_ERROR (-1)
#	define INVALID_SOCKET (-1)
#	define closesocket close
#	include "inet_socket.h"
#elif BX_PLATFORM_NACL
#	include <errno.h> // errno
#	include <stdio.h> // sscanf
#	include <string.h>
#	include <sys/time.h> // gettimeofday
#	include <sys/types.h> // fd_set
#	include "nacl_socket.h"
#endif // BX_PLATFORM_

#include <bx/debug.h>
#include <bx/handlealloc.h>
#include <bx/ringbuffer.h>
#include <bx/timer.h>

#include <new> // placement new

#if BNET_CONFIG_OPENSSL
#	include <openssl/err.h>
#	include <openssl/ssl.h>
#	include <openssl/crypto.h>
#else
#	define SSL_CTX void
#	define X509 void
#	define EVP_PKEY void
#endif // BNET_CONFIG_OPENSSL

#include <list>

namespace bnet
{
	extern reallocFn g_realloc;
	extern freeFn g_free;

	struct Internal
	{
		enum Enum
		{
			None,
			Disconnect,
			Notify,
		};
	};

	struct DisconnectReason
	{
		enum Enum
		{
			None,
			RecvFailed,
			SendFailed,
			InvalidMessageId,
		};
	};

	uint16_t ctxAccept(uint16_t _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, X509* _cert, EVP_PKEY* _key);
	void ctxPush(uint16_t _handle, MessageId::Enum _id);
	void ctxPush(Message* _msg);
	Message* msgAlloc(uint16_t _handle, uint16_t _size, bool _incoming = false, Internal::Enum _type = Internal::None);
	void msgRelease(Message* _msg);

	template<typename Ty> class FreeList
	{
	public:
		FreeList(uint16_t _max)
			: m_allocator(_max)
		{
			m_memBlock = g_realloc(NULL, _max*sizeof(Ty) );
		}

		~FreeList()
		{
			g_free(m_memBlock);
		}

		Ty* create()
		{
			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[m_allocator.alloc()];
			obj = ::new (obj) Ty;
			return obj;
		}

		template<typename Arg0> Ty* create(Arg0 _a0)
		{
			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[m_allocator.alloc()];
			obj = ::new (obj) Ty(_a0);
			return obj;
		}

		template<typename Arg0, typename Arg1> Ty* create(Arg0 _a0, Arg1 _a1)
		{
			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[m_allocator.alloc()];
			obj = ::new (obj) Ty(_a0, _a1);
			return obj;
		}

		template<typename Arg0, typename Arg1, typename Arg2> Ty* create(Arg0 _a0, Arg1 _a1, Arg2 _a2)
		{
			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[m_allocator.alloc()];
			obj = ::new (obj) Ty(_a0, _a1, _a2);
			return obj;
		}

		void destroy(Ty* _obj)
		{
			_obj->~Ty();
			m_allocator.free(getHandle(_obj) );
		}

		uint16_t getHandle(Ty* _obj) const
		{
			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			return (uint16_t)(_obj - first);
		}

		Ty* getFromHandle(uint16_t _index)
		{
			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			return &first[_index];
		}

		uint16_t getNumHandles() const
		{
			return m_allocator.getNumHandles();
		}

		uint16_t getMaxHandles() const
		{
			return m_allocator.getMaxHandles();
		}

		Ty* getFromHandleAt(uint16_t _at)
		{
			uint16_t handle = m_allocator.getHandleAt(_at);
			return getFromHandle(handle);
		}

	private:
		void* m_memBlock;
		HandleAlloc m_allocator;
	};

	class RecvRingBuffer
	{
	public:
		RecvRingBuffer(RingBufferControl& _control, char* _buffer)
			: m_control(_control)
			, m_write(_control.m_current)
			, m_reserved(0)
			, m_buffer(_buffer)
		{
		}

		~RecvRingBuffer()
		{
		}

		int recv(SOCKET _socket)
		{
			m_reserved += m_control.reserve(-1);
			uint32_t end = (m_write + m_reserved) % m_control.m_size;
			uint32_t wrap = end < m_write ? m_control.m_size - m_write : m_reserved;
			char* to = &m_buffer[m_write];

			int bytes = ::recv(_socket
							  , to
							  , wrap
							  , 0
							  );

			if (0 < bytes)
			{
				m_write += bytes;
				m_write %= m_control.m_size;
				m_reserved -= bytes;
				m_control.commit(bytes);
			}

			return bytes;
		}

#if BNET_CONFIG_OPENSSL
		int recv(SSL* _ssl)
		{
			m_reserved += m_control.reserve(-1);
			uint32_t end = (m_write + m_reserved) % m_control.m_size;
			uint32_t wrap = end < m_write ? m_control.m_size - m_write : m_reserved;
			char* to = &m_buffer[m_write];

			int bytes = SSL_read(_ssl
								, to
								, wrap
								);

			if (0 < bytes)
			{
				m_write += bytes;
				m_write %= m_control.m_size;
				m_reserved -= bytes;
				m_control.commit(bytes);
			}

			return bytes;
		}
#endif // BNET_CONFIG_OPENSSL

	private:
		RecvRingBuffer();
		RecvRingBuffer(const RecvRingBuffer&);
		void operator=(const RecvRingBuffer&);

		RingBufferControl& m_control;
		uint32_t m_write;
		uint32_t m_reserved;
		char* m_buffer;
	};

	class MessageQueue
	{
	public:
		MessageQueue()
		{
		}

		~MessageQueue()
		{
		}

		void push(Message* _msg)
		{
			m_queue.push_back(_msg);
		}

		Message* peek()
		{
			if (!m_queue.empty() )
			{
				return m_queue.front();
			}

			return NULL;
		}

		Message* pop()
		{
			if (!m_queue.empty() )
			{
				Message* msg = m_queue.front();
				m_queue.pop_front();
				return msg;
			}

			return NULL;
		}

	private:
		std::list<Message*> m_queue;
	};

} // namespace bnet

#endif // __BNET_P_H__

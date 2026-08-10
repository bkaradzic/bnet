/*
 * Copyright 2010-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
 */

#ifndef BNET_P_H_HEADER_GUARD
#define BNET_P_H_HEADER_GUARD

#include <bnet/bnet.h>
#include <bx/bx.h>

#include "config.h"

#if BX_PLATFORM_WINDOWS
#	if BX_PLATFORM_WINDOWS
#		if !defined(_WIN32_WINNT)
#			define _WIN32_WINNT 0x0501
#		endif
#		include <ws2tcpip.h>
#	endif
#	define socklen_t int32_t
#elif  BX_PLATFORM_LINUX \
	|| BX_PLATFORM_ANDROID \
	|| BX_PLATFORM_OSX \
	|| BX_PLATFORM_IOS
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
#endif // BX_PLATFORM_

#include <bx/debug.h>
#include <bx/handlealloc.h>
#include <bx/ringbuffer.h>
#include <bx/timer.h>
#include <bx/allocator.h>

#include <new> // placement new
#include <stdio.h> // sscanf

#include <list>

namespace bnet
{
	struct Internal
	{
		enum Enum
		{
			None,
			Disconnect,
			Notify,
		};
	};

	extern bx::AllocatorI* g_allocator;

	struct TlsContext;
	struct TlsConnection;

#if BNET_CONFIG_TLS
	/// Create TLS context used by outgoing (client) connections.
	TlsContext* tlsContextCreate();

	/// Create TLS context used by incoming (server) connections. Both `_cert`
	/// and `_key` are either inline PEM data, or path to PEM file.
	TlsContext* tlsServerContextCreate(const char* _cert, const char* _key);

	/// Increment TLS context reference count.
	TlsContext* tlsContextAddRef(TlsContext* _ctx);

	/// Decrement TLS context reference count, and destroy it when it reaches zero.
	void tlsContextDestroy(TlsContext* _ctx);

	TlsConnection* tlsConnect(TlsContext* _ctx, SOCKET _socket, const char* _hostname);
	TlsConnection* tlsAccept(TlsContext* _ctx, SOCKET _socket);
	void tlsDestroy(TlsConnection* _tls);
	int tlsHandshake(TlsConnection* _tls);
	int tlsRecv(TlsConnection* _tls, char* _data, int _len);
	int tlsSend(TlsConnection* _tls, const char* _data, int _len);
#else
	inline TlsContext* tlsContextCreate()
	{
		return NULL;
	}

	inline TlsContext* tlsServerContextCreate(const char* /*_cert*/, const char* /*_key*/)
	{
		return NULL;
	}

	inline TlsContext* tlsContextAddRef(TlsContext* /*_ctx*/)
	{
		return NULL;
	}

	inline void tlsContextDestroy(TlsContext* /*_ctx*/)
	{
	}

	inline TlsConnection* tlsConnect(TlsContext* /*_ctx*/, SOCKET /*_socket*/, const char* /*_hostname*/)
	{
		return NULL;
	}

	inline TlsConnection* tlsAccept(TlsContext* /*_ctx*/, SOCKET /*_socket*/)
	{
		return NULL;
	}

	inline void tlsDestroy(TlsConnection* /*_tls*/)
	{
	}

	inline int tlsHandshake(TlsConnection* /*_tls*/)
	{
		return -1;
	}

	inline int tlsRecv(TlsConnection* /*_tls*/, char* /*_data*/, int /*_len*/)
	{
		return -1;
	}

	inline int tlsSend(TlsConnection* /*_tls*/, const char* /*_data*/, int /*_len*/)
	{
		return -1;
	}
#endif // BNET_CONFIG_TLS

	Handle ctxAccept(Handle _listenHandle, SOCKET _socket, uint32_t _ip, uint16_t _port, bool _raw, TlsContext* _tlsCtx);
	void ctxPush(Handle _handle, MessageId::Enum _id);
	void ctxPush(Message* _msg);
	Message* msgAlloc(Handle _handle, uint16_t _size, bool _incoming = false, Internal::Enum _type = Internal::None);
	void msgRelease(Message* _msg);

	template<typename Ty>
	class FreeList
	{
	public:
		FreeList(uint16_t _max)
		{
			m_memBlock = bx::alloc(g_allocator, _max*sizeof(Ty) );
			m_handleAlloc = bx::createHandleAlloc(g_allocator, _max);
		}

		~FreeList()
		{
			bx::destroyHandleAlloc(g_allocator, m_handleAlloc);
			bx::free(g_allocator, m_memBlock);
		}

		Ty* create()
		{
			uint16_t handle = m_handleAlloc->alloc();
			if (handle == bx::kInvalidHandle) {
				return NULL;
			}

			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[handle];
			obj = ::new (obj) Ty;
			return obj;
		}

		template<typename Arg0> Ty* create(Arg0 _a0)
		{
			uint16_t handle = m_handleAlloc->alloc();
			if (handle == bx::kInvalidHandle) {
				return NULL;
			}

			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[handle];
			obj = ::new (obj) Ty(_a0);
			return obj;
		}

		template<typename Arg0, typename Arg1> Ty* create(Arg0 _a0, Arg1 _a1)
		{
			uint16_t handle = m_handleAlloc->alloc();
			if (handle == bx::kInvalidHandle) {
				return NULL;
			}

			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[handle];
			obj = ::new (obj) Ty(_a0, _a1);
			return obj;
		}

		template<typename Arg0, typename Arg1, typename Arg2> Ty* create(Arg0 _a0, Arg1 _a1, Arg2 _a2)
		{
			uint16_t handle = m_handleAlloc->alloc();
			if (handle == bx::kInvalidHandle) {
				return NULL;
			}

			Ty* first = reinterpret_cast<Ty*>(m_memBlock);
			Ty* obj = &first[handle];
			obj = ::new (obj) Ty(_a0, _a1, _a2);
			return obj;
		}

		void destroy(Ty* _obj)
		{
			_obj->~Ty();
			m_handleAlloc->free(getHandle(_obj) );
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
			return m_handleAlloc->getNumHandles();
		}

		uint16_t getMaxHandles() const
		{
			return m_handleAlloc->getMaxHandles();
		}

		Ty* getFromHandleAt(uint16_t _at)
		{
			uint16_t handle = m_handleAlloc->getHandleAt(_at);
			return getFromHandle(handle);
		}

	private:
		void* m_memBlock;
		bx::HandleAlloc* m_handleAlloc;
	};

	class RecvRingBuffer
	{
		BX_CLASS(RecvRingBuffer
			, NO_DEFAULT_CTOR
			, NO_COPY
			);

	public:
		RecvRingBuffer(bx::RingBufferControl& _control, char* _buffer)
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
			m_reserved += m_control.reserve(UINT32_MAX);
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

#if BNET_CONFIG_TLS
		int recv(TlsConnection* _tls)
		{
			m_reserved += m_control.reserve(UINT32_MAX);
			uint32_t end = (m_write + m_reserved) % m_control.m_size;
			uint32_t wrap = end < m_write ? m_control.m_size - m_write : m_reserved;
			char* to = &m_buffer[m_write];

			int bytes = tlsRecv(_tls
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
#endif // BNET_CONFIG_TLS

	private:
		bx::RingBufferControl& m_control;
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

#endif // BNET_P_H_HEADER_GUARD

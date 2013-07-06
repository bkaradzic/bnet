/*
 * Copyright 2010-2013 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef __BNET_H__
#define __BNET_H__

#include <stdint.h> // uint32_t
#include <stdlib.h> // size_t

#define BNET_HANDLE(_name) struct _name { uint16_t idx; }

namespace bnet
{
	BNET_HANDLE(Handle);

	static const Handle invalidHandle = { UINT16_MAX };
	static const uint16_t maxMessageSize = UINT16_MAX;

	struct MessageId
	{
		enum Enum
		{
			Notify = 1,
			IncomingConnection,
			LostConnection,
			ListenFailed,
			ConnectFailed,
			RawData,

			UserDefined = 8
		};
	};

	struct Message
	{
		uint8_t* data;
		uint16_t size;
		Handle handle;
	};

	typedef Message IncomingMessage;
	typedef Message OutgoingMessage;

	typedef void* (*ReallocFn)(void* _ptr, size_t _size);
	typedef void (*FreeFn)(void* _ptr);

	/// Returns is handle is valid.
	inline bool isValid(Handle _handle) { return invalidHandle.idx != _handle.idx; }

	/// Initialize networking.
	void init(uint16_t _maxConnections, uint16_t _maxListenSockets = 0, const char* _certs[] = NULL, ReallocFn _realloc = NULL, FreeFn _free = NULL);

	/// Shutdown networking.
	void shutdown();

	/// Listen for incoming connections.
	Handle listen(uint32_t _ip, uint16_t _port, bool _raw = false, const char* _cert = NULL, const char* _key = NULL);

	/// Stop listening for incoming connections.
	void stop(Handle _handle);

	/// Connect to remote host.
	Handle connect(uint32_t _ip, uint16_t _port, bool _raw = false, bool _secure = false);

	/// Disconnect from remote host.
	void disconnect(Handle _handle, bool _finish = false);

	// Notify sender when all prior messages are sent.
	void notify(Handle _handle, uint64_t _userData = 0);

	/// Allocate outgoing message.
	OutgoingMessage* alloc(Handle _handle, uint16_t _size);

	/// Send message.
	void send(OutgoingMessage* _msg);

	/// Process receive.
	IncomingMessage* recv();

	/// Release incoming message.
	void release(IncomingMessage* _msg);

	/// Convert name to IP address.
	uint32_t toIpv4(const char* _addr = "");

} // namespace bnet

#endif // __BNET_H__

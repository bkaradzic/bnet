/*
 * Copyright 2010-2012 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef __BNET_H__
#define __BNET_H__

#include <stdint.h> // uint32_t
#include <stdlib.h> // size_t

namespace bnet
{
	static const uint16_t invalidHandle = 0xffff;
	static const uint16_t maxMessageSize = 0xffff;

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
		uint16_t handle;
	};

	typedef Message IncomingMessage;
	typedef Message OutgoingMessage;

	typedef void* (*reallocFn)(void* _ptr, size_t _size);
	typedef void (*freeFn)(void* _ptr);

	/// Initialize networking.
	void init(uint16_t _maxConnections, uint16_t _maxListenSockets = 0, const char* _certs[] = NULL, reallocFn _realloc = NULL, freeFn _free = NULL);

	/// Shutdown networking.
	void shutdown();

	/// Listen for incoming connections.
	uint16_t listen(uint32_t _ip, uint16_t _port, bool _raw = false, const char* _cert = NULL, const char* _key = NULL);

	/// Stop listening for incoming connections.
	void stop(uint16_t _handle);

	/// Connect ot remote host.
	uint16_t connect(uint32_t _ip, uint16_t _port, bool _raw = false, bool _secure = false);

	/// Disconnect from remote host.
	void disconnect(uint16_t _handle, bool _finish = false);

	// Notify sender when all prior messages are sent.
	void notify(uint16_t _handle, uint64_t _userData = 0);

	/// Allocate outgoing message.
	OutgoingMessage* alloc(uint16_t _handle, uint16_t _size);

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

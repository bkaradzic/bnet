/*
 * Copyright 2010-2018 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet#license-bsd-2-clause
 */

#ifndef BNET_H_HEADER_GUARD
#define BNET_H_HEADER_GUARD

#include <stdint.h> // uint32_t
#include <stdlib.h> // NULL

#define BNET_HANDLE(_name) struct _name { uint16_t idx; }

namespace bx { struct AllocatorI; }

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

	struct DisconnectReason
	{
		enum Enum
		{
			None,
			HostClosed,
			RecvFailed,
			SendFailed,
			InvalidMessageId,
		};
	};

	/// Returned by `bnet::alloc` or `bnet::recv` call.
	struct Message
	{
		uint8_t* data; //< Message data.
		uint16_t size; //< Message size.
		Handle handle; //< Connection handle.
	};

	typedef Message IncomingMessage;
	typedef Message OutgoingMessage;

	/// Returns is handle is valid.
	inline bool isValid(Handle _handle) { return invalidHandle.idx != _handle.idx; }

	/// Initialize networking.
	///
	/// @param _maxConnections Maximum concurrent outgoing connections.
	/// @param _maxListenSockets Maximum number of listen ports.
	/// @param _certs SSL certificates.
	/// @param _allocator Custom allocator.
	///
	void init(uint16_t _maxConnections, uint16_t _maxListenSockets = 0, const char* _certs[] = NULL, bx::AllocatorI* _allocator = NULL);

	/// Shutdown networking.
	void shutdown();

	/// Start listen for incoming connections.
	///
	/// @returns Handle to connection object.
	///
	Handle listen(uint32_t _ip, uint16_t _port, bool _raw = false, const char* _cert = NULL, const char* _key = NULL);

	/// Stop listening for incoming connections.
	///
	/// @param _handle Handle to connection object.
	///
	void stop(Handle _handle);

	/// Connect to remote host.
	///
	/// @param _ip IPv4 address.
	/// @param _port Port.
	/// @param _raw Non-structured messages. When this is `false` bnet
	///   frames messages.
	/// @param _secure Create TLS/SSL connection.
	///
	/// @returns Handle to connection object.
	///
	Handle connect(uint32_t _ip, uint16_t _port, bool _raw = false, bool _secure = false);

	/// Disconnect from remote host.
	///
	/// @param _handle Handle to connection object.
	/// @param _finish Send all pending messages before closing
	///   connection.
	///
	void disconnect(Handle _handle, bool _finish = false);

	/// Notify sender when all prior messages are sent.
	void notify(Handle _handle, uint64_t _userData = 0);

	/// Allocate outgoing message.
	///
	/// @param _handle Handle to connection object.
	/// @param _size Message size.
	///
	/// @returns Outgoing message object.
	///
	OutgoingMessage* alloc(Handle _handle, uint16_t _size);

	/// Send message.
	///
	/// @param Message object allocated with `bnet::alloc` call.
	///
	void send(OutgoingMessage* _msg);

	/// Process receive.
	///
	/// @returns Incomming message object. Must be released by calling `bnet::release`.
	///
	IncomingMessage* recv();

	/// Release incoming message.
	///
	/// @param Message returned by `bnet::recv` call.
	///
	void release(IncomingMessage* _msg);

	/// Convert name to IP address.
	///
	/// @param _addr Name or IPv4 string.
	///
	/// @returns IPv4 address.
	///
	uint32_t toIpv4(const char* _addr = "");

} // namespace bnet

#endif // BNET_H_HEADER_GUARD

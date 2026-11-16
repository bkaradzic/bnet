/*
 * Copyright 2010-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
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

	/// Initialize networking. Functions can be called from any thread after it.
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
	/// @param _ip IPv4 address.
	/// @param _port Port.
	/// @param _raw Non-structured messages. When this is `false` bnet
	///   frames messages.
	/// @param _cert Certificate used to accept TLS/SSL connections. Either
	///   inline PEM data, or path to PEM file. When set `_key` must be set too.
	/// @param _key Private key matching `_cert`. Either inline PEM data, or
	///   path to PEM file.
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
	/// @param _hostname Host name used for TLS Server Name Indication (SNI).
	///   Ignored when `_secure` is `false`.
	///
	/// @returns Handle to connection object.
	///
	Handle connect(uint32_t _ip, uint16_t _port, bool _raw = false, bool _secure = false, const char* _hostname = NULL);

	/// Connect to remote host by name.
	///
	/// @param _host Host name or IPv4 string. When `_secure` is set the host
	///   name is also used for TLS Server Name Indication (SNI).
	/// @param _port Port.
	/// @param _raw Non-structured messages. When this is `false` bnet
	///   frames messages.
	/// @param _secure Create TLS/SSL connection.
	///
	/// @returns Handle to connection object.
	///
	Handle connect(const char* _host, uint16_t _port, bool _raw = false, bool _secure = false);

	/// Start listen for connections on local socket, from processes of the same user on the
	/// same machine. Socket file at `_path` is created, and removed when listening stops; one
	/// left by a process that ended is replaced. Listening fails when another process listens
	/// at `_path` already, `bnet::recv` returns `MessageId::ListenFailed`; telling that apart
	/// connects to it, the one that listens sees a connection that ends at once.
	///
	/// @param _path Socket file path. It must fit `sockaddr_un::sun_path` (about 100 bytes),
	///   and should be in directory only the user can access.
	/// @param _raw Non-structured messages. When this is `false` bnet
	///   frames messages.
	///
	/// @returns Handle to connection object.
	///
	/// @remarks Windows 10 version 1803 or later. On Linux and macOS processes of other users
	///   are refused, on Windows socket file's directory decides who can connect.
	///
	Handle listenLocal(const char* _path, bool _raw = false);

	/// Connect to local socket.
	///
	/// @param _path Socket file path, the one passed to `bnet::listenLocal`.
	/// @param _raw Non-structured messages. When this is `false` bnet
	///   frames messages.
	///
	/// @returns Handle to connection object. `bnet::recv` returns `MessageId::ConnectFailed`
	///   when nobody listens at `_path`.
	///
	Handle connectLocal(const char* _path, bool _raw = false);

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
	/// @returns Incoming message object. Must be released by calling `bnet::release`.
	///
	IncomingMessage* recv();

	/// Release incoming message.
	///
	/// @param Message returned by `bnet::recv` call.
	///
	void release(IncomingMessage* _msg);

	/// Wait until connection, or listen socket, has something for `bnet::recv`, or until
	/// `bnet::wake` is called, or `_msecs` passed.
	///
	/// @param _msecs Milliseconds to wait at most, negative waits until there's something.
	///
	void wait(int32_t _msecs = -1);

	/// Wake `bnet::wait`, from any thread.
	void wake();

	/// Convert name to IP address.
	///
	/// @param _addr Name or IPv4 string.
	///
	/// @returns IPv4 address.
	///
	uint32_t toIpv4(const char* _addr = "");

} // namespace bnet

#endif // BNET_H_HEADER_GUARD

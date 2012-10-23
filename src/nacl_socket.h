/*
 * Copyright 2010-2012 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef __NACL_SOCKET__
#define __NACL_SOCKET__

#include <bx/endian.h>

typedef int SOCKET;

#define socklen_t int32_t
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)

#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#define EINPROGRESS 115

#define IPPROTO_TCP 6

#define TCP_NODELAY 1

#define SOMAXCONN 128

#define SOCK_STREAM 1

#define PF_UNSPEC 0
#define AF_UNSPEC PF_UNSPEC

#define PF_INET 2
#define AF_INET PF_INET

#define SOL_SOCKET 1
#define SO_SNDBUF 7
#define SO_RCVBUF 8

#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)

typedef uint32_t in_addr_t;

struct in_addr
{
	in_addr_t s_addr;
};

struct sockaddr
{
	unsigned short int sa_family;
	unsigned char sa_data[14];
};

typedef unsigned short int sa_family_t;
typedef uint16_t in_port_t;

struct sockaddr_in
{
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
};

inline uint32_t htonl(uint32_t _hostlong)
{
	return bx::bigEndian(_hostlong);
}

inline uint16_t htons(uint16_t _hostshort)
{
	return bx::bigEndian(_hostshort);
}

inline uint32_t ntohl(uint32_t _hostlong)
{
	return bx::bigEndian(_hostlong);
}

inline uint16_t ntohs(uint16_t _hostshort)
{
	return bx::bigEndian(_hostshort);
}

extern int naclOpenSocket();
extern void naclCloseSocket(int _fd);
int naclConnect(int _fd, uint32_t _ip, uint16_t _port, bool _secure);
extern ssize_t naclSend(int _fd, const void* _buf, size_t _n);
extern ssize_t naclRecv(int _fd, void* _buf, size_t _n);
extern bool naclIsConnected(int _fd);

inline int socket(int _domain, int _type, int _protocol)
{
	return naclOpenSocket();
}

inline void closesocket(int _fd)
{
	naclCloseSocket(_fd);
}

inline int connectsocket(int _fd, uint32_t _ip, uint16_t _port, bool _secure)
{
	return naclConnect(_fd, _ip, _port, _secure);
}

inline ssize_t send(int _fd, const void* _buf, size_t _n, int _flags)
{
	return naclSend(_fd, _buf, _n);
}

inline ssize_t recv(int _fd, void* _buf, size_t _n, int _flags)
{
	return naclRecv(_fd, _buf, _n);
}

static bool issocketready(int _fd)
{	
	return naclIsConnected(_fd);
}

inline int setsockopt(int _fd, int _level, int _optname, const void* _optval, socklen_t _optlen)
{
	return 0;
}

inline int listen(int _fd, int _n)
{
	return INVALID_SOCKET;
}

inline int accept(int _fd, struct sockaddr* _addr, socklen_t* _addr_len)
{
	return INVALID_SOCKET;
}

inline int bind(int _fd, const struct sockaddr* _addr, socklen_t _len)
{
	return INVALID_SOCKET;
}

struct addrinfo
{
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

inline int getaddrinfo(const char* _name, const char* _service, const struct addrinfo* _req, struct addrinfo** _pai)
{
	return 0;
}

inline void freeaddrinfo(struct addrinfo* _ai)
{
}

#endif // __NACL_SOCKET__

#ifndef __INET_SOCKET__
#define __INET_SOCKET__

static int connectsocket(SOCKET socket, const char* _host, uint16_t _port, bool _secure)
{
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(_host);
	if (addr.sin_addr.s_addr == INADDR_NONE)
	{
		addr.sin_addr.s_addr = htonl(::bnet::toIpv4(_host));
	}
	addr.sin_port = htons(_port);

	union
	{
		sockaddr* sa;
		sockaddr_in* sain;
	} saintosa;
	saintosa.sain = &addr;
	
	return ::connect(socket, saintosa.sa, sizeof(addr));
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
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	int result = ::select( (int)socket + 1 /*nfds is ignored on windows*/, &rfds, &wfds, NULL, &timeout);
	return result > 0;
}

#endif // __INET_SOCKET__

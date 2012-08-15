#ifndef __INET_SOCKET__
#define __INET_SOCKET__

static int connectsocket(SOCKET socket, const char* _host, uint16_t _port)
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

#endif // __INET_SOCKET__

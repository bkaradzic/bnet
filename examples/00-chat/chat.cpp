/*
 * Copyright 2010-2013 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include <bnet.h>

#include <stdio.h>
#include <string.h>
#include <set>
#include <malloc.h>

#include <bx/string.h>
#include <bx/commandline.h>

static const char* s_certs[] = {
	"-----BEGIN CERTIFICATE-----\n"
	"MIICDTCCAXYCCQDQyM1G6kagwzANBgkqhkiG9w0BAQUFADBLMQswCQYDVQQGEwJV\n"
	"UzETMBEGA1UECBMKV2FzaGluZ3RvbjEQMA4GA1UEBxMHU2VhdHRsZTEVMBMGA1UE\n"
	"ChMMQ2FyYm9uIEdhbWVzMB4XDTExMDgxMzIxMDY1N1oXDTExMDkxMjIxMDY1N1ow\n"
	"SzELMAkGA1UEBhMCVVMxEzARBgNVBAgTCldhc2hpbmd0b24xEDAOBgNVBAcTB1Nl\n"
	"YXR0bGUxFTATBgNVBAoTDENhcmJvbiBHYW1lczCBnzANBgkqhkiG9w0BAQEFAAOB\n"
	"jQAwgYkCgYEArVlCwFQMdhbitUzhuXb8r5NNjr3mgJAfXqpgG19r8IQDI/ueD4DY\n"
	"ueLa+34VVWwaP27D3XIrAi7+WmGzUGf47b138F98E49HAYyoHnr/ww1mCGU4Jf8t\n"
	"6JDREeyjqaFKhU1+zNjsyonooL1tPpw7u5fwjdrc6PX0qCKHHdLU+Q0CAwEAATAN\n"
	"BgkqhkiG9w0BAQUFAAOBgQBm+bpTDyJttfpMeKkfmtZH828fX7qzdoj9Qs+G6Dyc\n"
	"NqROo3yQiZfT8rhvyU9MdEhQcE53Eh+RA4uqAz3vkQH39mRcyrcyO3ktvCUC+0YY\n"
	"WT7lu6St2t+/RgXY6ghaYCb8ko31HIJYcINZUXSBtpDYeZtRCS/nUb6LelMaOXDE\n"
	"1A==\n"
	"-----END CERTIFICATE-----\n"
	,
	NULL
};

static const char* s_key =
	"-----BEGIN RSA PRIVATE KEY-----\n"
	"MIICXAIBAAKBgQCtWULAVAx2FuK1TOG5dvyvk02OveaAkB9eqmAbX2vwhAMj+54P\n"
	"gNi54tr7fhVVbBo/bsPdcisCLv5aYbNQZ/jtvXfwX3wTj0cBjKgeev/DDWYIZTgl\n"
	"/y3okNER7KOpoUqFTX7M2OzKieigvW0+nDu7l/CN2tzo9fSoIocd0tT5DQIDAQAB\n"
	"AoGBAImw1Oyf1iYWl40avFDsyllLz9cJ0AVedQxkmGIlsT8iHLyAKFR4K627G+WX\n"
	"iKqJa2/nM3y6Kp9ZZH+2CxBbBcXBib9thsv3uZ0GPVU03RVZoOc+oia1BefhQ8qg\n"
	"3nZd8yWe99a+VJnDrPA+QJ/qrL7KrstqKcYkOB35Yt+iDCUBAkEA44/sAt77sIlJ\n"
	"1YB6RAL1G0Ocq3X9MQXGXFd6atRP+Lld1gfgvT1H01y6ERXCD4g6+dc+ICcIeuCi\n"
	"9nJULJ9VGQJBAMMC9jO+65P2n10CmnyIiYk3nzoWAsPxxyG64XymmG1PZU1hffV3\n"
	"wx1uztDs/v4F9OBxF+Xh+mC6Ovwofj7MrhUCQHGNvOjF2nSCXYyjet97VlIPkBtj\n"
	"Wj/fMNedc2HhpjJoVXHbJoNoE/JdwB+MavUTNtK7XK3wrGOcutUdwfEuZOkCQFLA\n"
	"VP1MTOcyxhlP24Jw5fwGUFjzsiS32kpj5P9iKlhoUpJthme9dFxvAvABQYtFt83t\n"
	"77grFnYpUJJkFH5NmKkCQBk80OK/HBQNlmDkcdFpSDgAH3qHxcljL4XOopDNz5fe\n"
	"cW6N7/0QrO2GhTp7JNXIdYx35iTHTY6poO0uNAwdrgE=\n"
	"-----END RSA PRIVATE KEY-----\n"
	;

void printMsg(const bnet::Message* _msg)
{
	uint16_t len = _msg->size;
	char* temp = (char*)alloca(len);
	memcpy(temp, &_msg->data[1], len-1);
	temp[len-1] = '\0';
	printf("UserMessage %d: %s\n", _msg->data[0], temp);
}

int main(int _argc, const char* _argv[])
{
	bx::CommandLine cmdLine(_argc, _argv);

	uint16_t port = 1337;
	const char* portOpt = cmdLine.findOption('p');
	if (NULL != portOpt)
	{
		port = atoi(portOpt);
	}

	bool server = cmdLine.hasArg('s', "server");
	if (server)
	{
		bnet::init(10, 1, s_certs);
		uint32_t ip = bnet::toIpv4("localhost");
		bnet::listen(ip, port, false, s_certs[0], s_key);
	}
	else
	{
		bnet::init(1, 0, s_certs);

		const char* host = cmdLine.findOption('h', "host");
		uint32_t ip = bnet::toIpv4(NULL == host ? "localhost" : host);
		bnet::Handle handle = bnet::connect(ip, port, false, true);

		const char* hello = "hello there!";
		uint16_t len = (uint16_t)strlen(hello);
		bnet::Message* msg = bnet::alloc(handle, len+1);
		msg->data[0] = bnet::MessageId::UserDefined;
		memcpy(&msg->data[1], hello, len);
		bnet::send(msg);
	}

	bool cont = true;
	while (server || cont)
	{
		bnet::Message* msg = bnet::recv();
		if (NULL != msg)
		{
			if (bnet::MessageId::UserDefined > msg->data[0])
			{
				switch (msg->data[0])
				{
				case bnet::MessageId::ListenFailed:
					printf("Listen failed port is already in use?\n");
					cont = server = false;
					break;

				case bnet::MessageId::IncomingConnection:
					{
						{
							bnet::Handle listen = { *( (uint16_t*)&msg->data[1]) };
							uint32_t ip = *( (uint32_t*)&msg->data[3]);
							uint16_t port = *( (uint16_t*)&msg->data[7]);

							printf("%d.%d.%d.%d:%d connected\n"
								, ip>>24
								, (ip>>16)&0xff
								, (ip>>8)&0xff
								, ip&0xff
								, port
								);

							bnet::stop(listen);
						}

						uint32_t ip = bnet::toIpv4("localhost");
						bnet::listen(ip, port, false, s_certs[0], s_key);
					}
					break;

				case bnet::MessageId::LostConnection:
					printf("disconnected\n");
					cont = false;
					break;

				case bnet::MessageId::ConnectFailed:
					printf("%d\n", msg->data[0]);
					cont = false;
					bnet::disconnect(msg->handle);
					break;

				case bnet::MessageId::RawData:
					break;

				default:
					// fail...
					break;
				}
			}
			else
			{
				printMsg(msg);

				bnet::Handle handle = msg->handle;

				{
					const char* ping = "ping!";
					const char* pong = "pong!";
					const char* hello = server ? ping : pong;
					uint16_t len = (uint16_t)strlen(hello);
					bnet::Message* msg = bnet::alloc(handle, len+1);
					msg->data[0] = bnet::MessageId::UserDefined+1;
					memcpy(&msg->data[1], hello, len);
					bnet::send(msg);
				}
			}

			bnet::release(msg);
		}
	}

	bnet::shutdown();
	return EXIT_SUCCESS;
}

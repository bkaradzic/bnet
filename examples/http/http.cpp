/*
 * Copyright 2010-2011 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <set>
#include <malloc.h>
#include "bnet.h"
#include "../common/url.h"

#ifndef snprintf
#	define snprintf _snprintf
#endif // snprintf

uint16_t httpSendRequest(uint32_t _ip, uint16_t _port, const char* _request, bool secure)
{
	uint16_t handle = bnet::connect(_ip, _port, true, secure);

	bnet::Message* out = bnet::alloc(handle, (uint32_t)strlen(_request) );
	memcpy(out->data, _request, out->size);
	bnet::send(out);
	bnet::notify(handle, (void*)0x123456789ABCDEF);

	return handle;
}

static const char* s_cert[] = {
	// Equifax Secure CA
	"-----BEGIN CERTIFICATE-----\n"
	"MIIDIDCCAomgAwIBAgIENd70zzANBgkqhkiG9w0BAQUFADBOMQswCQYDVQQGEwJV\n"
	"UzEQMA4GA1UEChMHRXF1aWZheDEtMCsGA1UECxMkRXF1aWZheCBTZWN1cmUgQ2Vy\n"
	"dGlmaWNhdGUgQXV0aG9yaXR5MB4XDTk4MDgyMjE2NDE1MVoXDTE4MDgyMjE2NDE1\n"
	"MVowTjELMAkGA1UEBhMCVVMxEDAOBgNVBAoTB0VxdWlmYXgxLTArBgNVBAsTJEVx\n"
	"dWlmYXggU2VjdXJlIENlcnRpZmljYXRlIEF1dGhvcml0eTCBnzANBgkqhkiG9w0B\n"
	"AQEFAAOBjQAwgYkCgYEAwV2xWGcIYu6gmi0fCG2RFGiYCh7+2gRvE4RiIcPRfM6f\n"
	"BeC4AfBONOziipUEZKzxa1NfBbPLZ4C/QgKO/t0BCezhABRP/PvwDN1Dulsr4R+A\n"
	"cJkVV5MW8Q+XarfCaCMczE1ZMKxRHjuvK9buY0V7xdlfUNLjUA86iOe/FP3gx7kC\n"
	"AwEAAaOCAQkwggEFMHAGA1UdHwRpMGcwZaBjoGGkXzBdMQswCQYDVQQGEwJVUzEQ\n"
	"MA4GA1UEChMHRXF1aWZheDEtMCsGA1UECxMkRXF1aWZheCBTZWN1cmUgQ2VydGlm\n"
	"aWNhdGUgQXV0aG9yaXR5MQ0wCwYDVQQDEwRDUkwxMBoGA1UdEAQTMBGBDzIwMTgw\n"
	"ODIyMTY0MTUxWjALBgNVHQ8EBAMCAQYwHwYDVR0jBBgwFoAUSOZo+SvSspXXR9gj\n"
	"IBBPM5iQn9QwHQYDVR0OBBYEFEjmaPkr0rKV10fYIyAQTzOYkJ/UMAwGA1UdEwQF\n"
	"MAMBAf8wGgYJKoZIhvZ9B0EABA0wCxsFVjMuMGMDAgbAMA0GCSqGSIb3DQEBBQUA\n"
	"A4GBAFjOKer89961zgK5F7WF0bnj4JXMJTENAKaSbn+2kmOeUJXRmm/kEd5jhW6Y\n"
	"7qj/WsjTVbJmcVfewCHrPSqnI0kBBIZCe/zuf6IWUrVnZ9NA2zsmWLIodz2uFHdh\n"
	"1voqZiegDfqnc1zqcPGUIWVEX/r87yloqaKHee9570+sB3c4\n"
	"-----END CERTIFICATE-----\n"
	,
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

int main(int _argc, const char* _argv[])
{
	bnet::init(1, 0, s_cert);

//	const char* url = "http://gravatar.com/avatar/cc47d6856403a62afc5c74d269b7e610.png";
	const char* url = "https://encrypted.google.com/";

	char* tokens[UrlToken::Count];
	char temp[1024];
	tokenizeUrl(url, temp, sizeof(temp), tokens);

	bool secure = false;
	uint16_t port = 0;
	if (0 == _stricmp(tokens[UrlToken::Scheme], "http") )
	{
		port = 80;
	}
	else if (0 == _stricmp(tokens[UrlToken::Scheme], "https") )
	{
		port = 443;
		secure = true;
	}

	if (0 != port)
	{
		uint32_t ip = bnet::toIpv4(tokens[UrlToken::Host]);
		if ('\0' != tokens[UrlToken::Port][0])
		{
			port = atoi(tokens[UrlToken::Port]);
		}

		char header[1024];
		snprintf(header
				, sizeof(header)
				, "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n"
				, tokens[UrlToken::Path]
				, tokens[UrlToken::Host]
				);

		uint16_t handle = httpSendRequest(ip, port, header, secure);

		uint32_t size = 0;
		uint8_t* data = NULL;

		bool cont = bnet::InvalidHandle != handle;
		if (cont)
		{
			printf("Connecting to %s (%d.%d.%d.%d:%d)\n"
				, url
				, ip>>24
				, (ip>>16)&0xff
				, (ip>>8)&0xff
				, ip&0xff
				, port
				);
		}

		while (cont)
		{
			bnet::Message* msg = bnet::recv();
			if (NULL != msg)
			{
				if (bnet::MessageId::UserDefined > msg->data[0])
				{
					switch (msg->data[0])
					{
					case bnet::MessageId::Notify:
						printf("notify!\n");
						break;

					case bnet::MessageId::LostConnection:
					case bnet::MessageId::ConnectFailed:
						cont = false;
						if (NULL != data)
						{
							FILE* file = fopen("http.txt", "wb");
							fwrite(data, 1, size, file);
							fclose(file);
							printf("Received total %d. Data saved into http.txt.\n", size);
						}
						break;

					case bnet::MessageId::RawData:
						{
							printf("# raw %d bytes.\n", msg->size);
							uint32_t pos = size;
							size += msg->size-1;
							data = (uint8_t*)realloc(data, size+1);
							memcpy(&data[pos], &msg->data[1], msg->size-1);
							data[size-1] = '\0';
						}
						break;
					}
				}

				bnet::free(msg);
			}
		}
	}

	bnet::shutdown();
	return 0;
}


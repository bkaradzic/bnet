/*
 * Copyright 2010-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
 */

#include <bnet/bnet.h>

#include <malloc.h>

#include <bx/string.h>
#include <bx/url.h>
#include <bx/file.h>

bnet::Handle httpSendRequest(uint32_t _ip, uint16_t _port, const char* _request, bool secure)
{
	bnet::Handle handle = bnet::connect(_ip, _port, true, secure);

	bnet::Message* out = bnet::alloc(handle, (uint16_t)bx::strLen(_request) );
	bx::memCopy(out->data, _request, out->size);
	bnet::send(out);
	bnet::notify(handle, UINT64_C(0x123456789ABCDEF) );

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

int main(int /*_argc*/, const char* /*_argv*/[])
{
	bnet::init(1, 0, s_cert);

	const char* url = "http://gravatar.com/avatar/cc47d6856403a62afc5c74d269b7e610.png";
//	const char* url = "https://encrypted.google.com/";

	bx::UrlView urlView;
	urlView.parse(url);

	bool secure = false;
	uint32_t port = 0;
	if (0 == bx::strCmpI("http", urlView.get(bx::UrlView::Scheme) ) )
	{
		port = 80;
	}
	else if (0 == bx::strCmpI("https", urlView.get(bx::UrlView::Scheme) ) )
	{
		port = 443;
		secure = true;
	}

	if (0 != port)
	{
		char host[1024];
		strCopy(host, BX_COUNTOF(host), urlView.get(bx::UrlView::Host) );

		uint32_t ip = bnet::toIpv4(host);
		if (!urlView.get(bx::UrlView::Port).isEmpty() )
		{
			bx::fromString(&port, urlView.get(bx::UrlView::Port) );
		}

		char path[1024];
		strCopy(path, BX_COUNTOF(path), urlView.get(bx::UrlView::Path) );

		char header[1024];
		bx::snprintf(header
				, sizeof(header)
				, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n"
				, path
				, host
				);

		bnet::Handle handle = httpSendRequest(ip, uint16_t(port), header, secure);

		uint32_t size = 0;
		uint8_t* data = NULL;

		bool cont = bnet::isValid(handle);
		if (cont)
		{
			bx::printf("Connecting to %s (%d.%d.%d.%d:%d)\n"
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
						bx::printf("notify!\n");
						break;

					case bnet::MessageId::LostConnection:
					case bnet::MessageId::ConnectFailed:
						cont = false;
						if (NULL != data)
						{
							bx::FileWriter writer;
							bx::Error err;
							if (bx::open(&writer, "http.txt", false, &err) )
							{
								bx::write(&writer, data, size, &err);
								bx::close(&writer);

								bx::printf("Received total %d. Data saved into http.txt.\n", size);
							}
							else
							{
								bx::printf("Failed to open http.txt!");
							}
						}
						break;

					case bnet::MessageId::RawData:
						{
							bx::printf("# raw %d bytes.\n", msg->size);
							uint32_t pos = size;
							size += msg->size-1;
							data = (uint8_t*)realloc(data, size+1);
							bx::memCopy(&data[pos], &msg->data[1], msg->size-1);
							data[size-1] = '\0';
						}
						break;
					}
				}

				bnet::release(msg);
			}
		}
	}

	bnet::shutdown();
	return 0;
}


/*
 * Copyright 2010-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
 */

#include <bnet/bnet.h>

#include <stdio.h>
#include <string.h>
#include <set>
#include <malloc.h>

#include <bx/string.h>
#include <bx/commandline.h>

static const char* s_certs[] = {
	"-----BEGIN CERTIFICATE-----\n"
	"MIIC9DCCAdygAwIBAgIIcV6UGFS1sM0wDQYJKoZIhvcNAQELBQAwIzENMAsGA1UE\n"
	"ChMEYm5ldDESMBAGA1UEAxMJbG9jYWxob3N0MB4XDTI2MDEwMTAwMDAwMFoXDTQ2\n"
	"MDEwMTAwMDAwMFowIzENMAsGA1UEChMEYm5ldDESMBAGA1UEAxMJbG9jYWxob3N0\n"
	"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAurfNxzyA4UHNsG/9ETmg\n"
	"1VS50yiqGPPt3tYjPjYZGc75lUE9C+l+CKO91SjRyGb3yhHgW/jO8la+rsHl8ayz\n"
	"kO5xI7H9f7LSnM2wEjpF7i7hirA1cJgoMWDqHFVADSqy7ak3YevtTe77Gp/EzuUm\n"
	"figeDchdHpSBD17X42hKZ8Py8kKUSns74SRXXAbMMT4iW8kIjUTwpCrvPtgWDTKQ\n"
	"sYQvbz1c3DQbDgTzTUQyatT/dKmHABrS4cy1hlN72M4lYTnSgXt0Y/70n4GNXjE/\n"
	"px/ZBIeXSHdA/lgqGY9KjOsbVPZkg/yKPnedmMjDFHLmuOWJTHL9nNG+DZPg/Mhu\n"
	"YQIDAQABoywwKjAaBgNVHREEEzARgglsb2NhbGhvc3SHBH8AAAEwDAYDVR0TAQH/\n"
	"BAIwADANBgkqhkiG9w0BAQsFAAOCAQEAL4EC5e1U3rCmyw/OsKrhVP2Ba4vStQeN\n"
	"u2V0bFgMz500sNnYxM+wjyFZI3St0uZKWqJvpH9ByubuDaMu3Z1R5ifMWNLVYCR8\n"
	"koZFWiXpO9QR0osf3SgWf+EDWpnOiqChuSGyULVm1FaKwghzIF32B9hkP1vUA2qG\n"
	"UHFw7AQUNRZcGPFHGfd6R5sZlWyKXeA5hf5beB4Kf+MQEGqAgp4UqJczEpO2cO7v\n"
	"uinq9WKH54kd+4BV93y9cG46E91IF5LHKRBbEg0KPQAU24dmNIoC4iLlrrYTnJVi\n"
	"fjPHdrPCGpufDOjoCTh/04+Dqco1yseRKnQ4OEMQo24vinQx5ZlXZw==\n"
	"-----END CERTIFICATE-----\n"
	,
	NULL
};

static const char* s_key =
	"-----BEGIN PRIVATE KEY-----\n"
	"MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQC6t83HPIDhQc2w\n"
	"b/0ROaDVVLnTKKoY8+3e1iM+NhkZzvmVQT0L6X4Io73VKNHIZvfKEeBb+M7yVr6u\n"
	"weXxrLOQ7nEjsf1/stKczbASOkXuLuGKsDVwmCgxYOocVUANKrLtqTdh6+1N7vsa\n"
	"n8TO5SZ+KB4NyF0elIEPXtfjaEpnw/LyQpRKezvhJFdcBswxPiJbyQiNRPCkKu8+\n"
	"2BYNMpCxhC9vPVzcNBsOBPNNRDJq1P90qYcAGtLhzLWGU3vYziVhOdKBe3Rj/vSf\n"
	"gY1eMT+nH9kEh5dId0D+WCoZj0qM6xtU9mSD/Io+d52YyMMUcua45YlMcv2c0b4N\n"
	"k+D8yG5hAgMBAAECggEAYX0aeg5DFFmxNZAjR90Y9om4RGIU/dZOumyAFjlUvb7t\n"
	"XVqkrxWIcqZbtXIMpl/svJq81AiCtNFJ2iDvGFIdp2x3sjV6sDQD908FwV6fqI7d\n"
	"v/Xk0RcA1VdOpOB2AunqZmBVxFDUpD5E6U/GCGhowrHbj0KwLAlJoWbZvah0QNyP\n"
	"1dNAX2bslp0igpIQbUzc9Voo3iY8FoKJTk6OJ26+WTNovzod0rrbiMbp3bd2kh5x\n"
	"Aip50gdUtfzSR7H8FRaP6D9e7SMDHSlYnp+puS7abl8zQ8uz7Q4m0s8cA3jbpS4s\n"
	"rvikgpVY5F/5oL5toJs4PPBEt5gHNMNFxfv0XWOeKQKBgQDPD8f5F36ZFSlW3rn4\n"
	"B5uqK2LayPoIzC37S2QSjRxUolwtHkJeVA1u8Ihw121TRRbOs8NEh+GNBArOYKgg\n"
	"E3a6oiQaUHq2jTvl0M2yF/VGG5naMN1YnBOShXMxfx/03PAdEY8EQoNbYu8c09CJ\n"
	"Ai1YZRgsC+ghLIZl699SHx3j3wKBgQDm2SJLN7hrkn7BtReofaBq6lWkpQMNVcqW\n"
	"2HnkAOkbseT5yGC4FIrvx7NnU/A/H1x1m7DMH9gkrwRSN7pDX5L2OXEWzEXxaihr\n"
	"Bh97Ysd2MQ+BzfEkG81s1nzeP9AqiL/FaSUvH5k1IQtWpL05tFu5glFqD7aETSSW\n"
	"HQ3LSnn1vwKBgFjxJ23Y6Llq/JnjDDD9W6FKB6mBAN38jpfN94t8b7nvD/cVc16/\n"
	"bhHEYmdOMhi9qaFaWDs8vubq4JVrsWwt0Cc09JsVDNETc7Iw8dpZLjNSMdEmgj3I\n"
	"tSOQDT4qpBhzOvTRkQQ8ad48bgeM+JuRgtbgffSVnL17ObPYENJeqWEzAoGBAK0O\n"
	"VgEhUmWCOvgoNAYht2KvLWjyMymKCQewXSAp9pbGc6s1JhyZedZrVPi/GjmX3w5j\n"
	"mtRLgxNtCMZB9KaRPXDMexTmKgDi3k3tFyi+Ul0uRju/EWlKVmOjH5TVLc7VGT56\n"
	"pl9/RrFnhkJ72UcrCCA3q6ThBqiD1EucmDywJmGtAoGBAKHH2/qxhGKLEhsAjPRm\n"
	"n8eUqXglrA/QlzEnU5gU5b0/NOT0xf6TTChjjn3wA0NltVpcQeP6erCAgs4hkIc7\n"
	"Dj9sZitI8s/UrnOL4b4xZigmP5KUJCm8zxfCCkkRSgwmLqwsRS6OdJPdCxIqnf1h\n"
	"ycthYHCDzLtfl5SSzgbpmuAu\n"
	"-----END PRIVATE KEY-----\n"
	;

void printMsg(const bnet::Message* _msg)
{
	uint16_t len = _msg->size;
	char* temp = (char*)BX_STACK_ALLOC(len);
	bx::memCopy(temp, &_msg->data[1], len-1);
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
		int32_t result;
		if (bx::fromString(&result, portOpt)
		&&  result < UINT16_MAX)
		{
			port = uint16_t(result);
		}
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
		bx::memCopy(&msg->data[1], hello, len);
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
							uint32_t rip = *( (uint32_t*)&msg->data[3]);
							uint16_t rport = *( (uint16_t*)&msg->data[7]);

							printf("%d.%d.%d.%d:%d connected\n"
								, rip>>24
								, (rip>>16)&0xff
								, (rip>>8)&0xff
								, rip&0xff
								, rport
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
					bnet::Message* omsg = bnet::alloc(handle, len+1);
					omsg->data[0] = bnet::MessageId::UserDefined+1;
					bx::memCopy(&omsg->data[1], hello, len);
					bnet::send(omsg);
				}
			}

			bnet::release(msg);
		}
	}

	bnet::shutdown();
	return bx::kExitSuccess;
}

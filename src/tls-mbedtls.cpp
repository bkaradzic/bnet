/*
 * Copyright 2010-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
 */

#include "bnet_p.h"

#if BNET_CONFIG_MBEDTLS

#include <bx/allocator.h>
#include <bx/string.h>

#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

namespace bnet
{
	static bool tlsWouldBlock()
	{
#if BX_PLATFORM_WINDOWS
		return WSAEWOULDBLOCK == WSAGetLastError();
#else
		return EWOULDBLOCK == errno;
#endif // BX_PLATFORM_WINDOWS
	}

	static void tlsSetWouldBlock()
	{
#if BX_PLATFORM_WINDOWS
		WSASetLastError(WSAEWOULDBLOCK);
#else
		errno = EWOULDBLOCK;
#endif // BX_PLATFORM_WINDOWS
	}

	struct TlsContext
	{
		mbedtls_entropy_context  m_entropy;
		mbedtls_ctr_drbg_context m_drbg;
		mbedtls_ssl_config       m_conf;
		mbedtls_x509_crt         m_cert;
		mbedtls_pk_context       m_key;
		int32_t                  m_refCount;
		bool                     m_server;
	};

	struct TlsConnection
	{
		enum Enum
		{
			Handshake,
			Established,
			Failed,
		};

		mbedtls_ssl_context m_ssl;
		TlsContext*         m_ctx;
		SOCKET              m_socket;
		uint8_t             m_state;
		char                m_host[256];
	};

	static int tlsBioSend(void* _ctx, const unsigned char* _buf, size_t _len)
	{
		SOCKET socket = (SOCKET)(uintptr_t)_ctx;
		int bytes = ::send(socket, (const char*)_buf, int(_len), 0);
		if (0 > bytes)
		{
			return tlsWouldBlock() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
		}

		return bytes;
	}

	static int tlsBioRecv(void* _ctx, unsigned char* _buf, size_t _len)
	{
		SOCKET socket = (SOCKET)(uintptr_t)_ctx;
		int bytes = ::recv(socket, (char*)_buf, int(_len), 0);
		if (0 == bytes)
		{
			return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
		}

		if (0 > bytes)
		{
			return tlsWouldBlock() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
		}

		return bytes;
	}

	static bool tlsIsPem(const char* _str)
	{
		return !bx::strFind(bx::StringView(_str), "-----BEGIN").isEmpty();
	}

	static int tlsParseCert(mbedtls_x509_crt* _crt, const char* _cert)
	{
		if (tlsIsPem(_cert) )
		{
			// PEM data must be NUL terminated, and the terminator is part of the length.
			return mbedtls_x509_crt_parse(_crt, (const unsigned char*)_cert, bx::strLen(_cert)+1);
		}

#if defined(MBEDTLS_FS_IO)
		return mbedtls_x509_crt_parse_file(_crt, _cert);
#else
		return MBEDTLS_ERR_X509_FILE_IO_ERROR;
#endif // defined(MBEDTLS_FS_IO)
	}

	static int tlsParseKey(mbedtls_pk_context* _pk, const char* _key, mbedtls_ctr_drbg_context* _drbg)
	{
		if (tlsIsPem(_key) )
		{
			return mbedtls_pk_parse_key(_pk
				, (const unsigned char*)_key
				, bx::strLen(_key)+1
				, NULL
				, 0
				, mbedtls_ctr_drbg_random
				, _drbg
				);
		}

#if defined(MBEDTLS_FS_IO)
		return mbedtls_pk_parse_keyfile(_pk, _key, NULL, mbedtls_ctr_drbg_random, _drbg);
#else
		return MBEDTLS_ERR_PK_FILE_IO_ERROR;
#endif // defined(MBEDTLS_FS_IO)
	}

	static void tlsContextFree(TlsContext* _ctx)
	{
		mbedtls_pk_free(&_ctx->m_key);
		mbedtls_x509_crt_free(&_ctx->m_cert);
		mbedtls_ssl_config_free(&_ctx->m_conf);
		mbedtls_ctr_drbg_free(&_ctx->m_drbg);
		mbedtls_entropy_free (&_ctx->m_entropy);
		bx::deleteObject(g_allocator, _ctx);
	}

	static TlsContext* tlsContextCreate(bool _server, const char* _cert, const char* _key)
	{
		TlsContext* ctx = BX_NEW(g_allocator, TlsContext);
		ctx->m_refCount = 1;
		ctx->m_server   = _server;

		mbedtls_entropy_init (&ctx->m_entropy);
		mbedtls_ctr_drbg_init(&ctx->m_drbg);
		mbedtls_ssl_config_init(&ctx->m_conf);
		mbedtls_x509_crt_init(&ctx->m_cert);
		mbedtls_pk_init(&ctx->m_key);

		int err = mbedtls_ctr_drbg_seed(&ctx->m_drbg, mbedtls_entropy_func, &ctx->m_entropy, NULL, 0);
		if (0 == err)
		{
			err = mbedtls_ssl_config_defaults(&ctx->m_conf
				, _server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT
				, MBEDTLS_SSL_TRANSPORT_STREAM
				, MBEDTLS_SSL_PRESET_DEFAULT
				);
		}

		if (0 != err)
		{
			BX_TRACE("mbedTLS: context init failed -0x%04x.", -err);
			tlsContextFree(ctx);
			return NULL;
		}

		mbedtls_ssl_conf_authmode(&ctx->m_conf, MBEDTLS_SSL_VERIFY_NONE);
		mbedtls_ssl_conf_rng(&ctx->m_conf, mbedtls_ctr_drbg_random, &ctx->m_drbg);

		if (_server)
		{
			err = tlsParseCert(&ctx->m_cert, _cert);
			if (0 != err)
			{
				BX_TRACE("mbedTLS: certificate parse failed -0x%04x.", -err);
				tlsContextFree(ctx);
				return NULL;
			}

			err = tlsParseKey(&ctx->m_key, _key, &ctx->m_drbg);
			if (0 != err)
			{
				BX_TRACE("mbedTLS: private key parse failed -0x%04x.", -err);
				tlsContextFree(ctx);
				return NULL;
			}

			err = mbedtls_ssl_conf_own_cert(&ctx->m_conf, &ctx->m_cert, &ctx->m_key);
			if (0 != err)
			{
				BX_TRACE("mbedTLS: certificate/private key mismatch -0x%04x.", -err);
				tlsContextFree(ctx);
				return NULL;
			}
		}

		return ctx;
	}

	TlsContext* tlsContextCreate()
	{
		return tlsContextCreate(false, NULL, NULL);
	}

	TlsContext* tlsServerContextCreate(const char* _cert, const char* _key)
	{
		if (NULL == _cert
		||  NULL == _key)
		{
			return NULL;
		}

		return tlsContextCreate(true, _cert, _key);
	}

	TlsContext* tlsContextAddRef(TlsContext* _ctx)
	{
		if (NULL != _ctx)
		{
			++_ctx->m_refCount;
		}

		return _ctx;
	}

	void tlsContextDestroy(TlsContext* _ctx)
	{
		if (NULL != _ctx
		&&  0 == --_ctx->m_refCount)
		{
			tlsContextFree(_ctx);
		}
	}

	static TlsConnection* tlsCreate(TlsContext* _ctx, SOCKET _socket, const char* _hostname)
	{
		if (NULL == _ctx)
		{
			return NULL;
		}

		TlsConnection* tls = BX_NEW(g_allocator, TlsConnection);
		tls->m_ctx    = tlsContextAddRef(_ctx);
		tls->m_socket = _socket;
		tls->m_state  = TlsConnection::Handshake;

		if (NULL != _hostname)
		{
			bx::strCopy(tls->m_host, sizeof(tls->m_host), _hostname);
		}
		else
		{
			tls->m_host[0] = '\0';
		}

		mbedtls_ssl_init(&tls->m_ssl);

		if (0 != mbedtls_ssl_setup(&tls->m_ssl, &_ctx->m_conf) )
		{
			mbedtls_ssl_free(&tls->m_ssl);
			tlsContextDestroy(tls->m_ctx);
			bx::deleteObject(g_allocator, tls);
			return NULL;
		}

		if ('\0' != tls->m_host[0])
		{
			mbedtls_ssl_set_hostname(&tls->m_ssl, tls->m_host);
		}

		mbedtls_ssl_set_bio(&tls->m_ssl, (void*)(uintptr_t)_socket, tlsBioSend, tlsBioRecv, NULL);

		return tls;
	}

	TlsConnection* tlsConnect(TlsContext* _ctx, SOCKET _socket, const char* _hostname)
	{
		return tlsCreate(_ctx, _socket, _hostname);
	}

	TlsConnection* tlsAccept(TlsContext* _ctx, SOCKET _socket)
	{
		return tlsCreate(_ctx, _socket, NULL);
	}

	void tlsDestroy(TlsConnection* _tls)
	{
		if (NULL == _tls)
		{
			return;
		}

		mbedtls_ssl_free(&_tls->m_ssl);
		tlsContextDestroy(_tls->m_ctx);
		bx::deleteObject(g_allocator, _tls);
	}

	int tlsHandshake(TlsConnection* _tls)
	{
		if (TlsConnection::Established == _tls->m_state)
		{
			return 1;
		}

		if (TlsConnection::Failed == _tls->m_state)
		{
			return -1;
		}

		int err = mbedtls_ssl_handshake(&_tls->m_ssl);
		if (0 == err)
		{
			_tls->m_state = TlsConnection::Established;
			return 1;
		}

		if (MBEDTLS_ERR_SSL_WANT_READ  == err
		||  MBEDTLS_ERR_SSL_WANT_WRITE == err)
		{
			return 0;
		}

		BX_TRACE("mbedTLS: handshake failed -0x%04x.", -err);
		_tls->m_state = TlsConnection::Failed;
		return -1;
	}

	int tlsRecv(TlsConnection* _tls, char* _data, int _len)
	{
		if (TlsConnection::Established != _tls->m_state)
		{
			tlsSetWouldBlock();
			return -1;
		}

		int bytes = mbedtls_ssl_read(&_tls->m_ssl, (unsigned char*)_data, _len);
		if (0 <= bytes)
		{
			return bytes;
		}

		if (MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY == bytes)
		{
			return 0;
		}

		if (MBEDTLS_ERR_SSL_WANT_READ  == bytes
		||  MBEDTLS_ERR_SSL_WANT_WRITE == bytes)
		{
			tlsSetWouldBlock();
			return -1;
		}

		return -1;
	}

	int tlsSend(TlsConnection* _tls, const char* _data, int _len)
	{
		if (TlsConnection::Established != _tls->m_state)
		{
			return -1;
		}

		int bytes = mbedtls_ssl_write(&_tls->m_ssl, (const unsigned char*)_data, _len);
		if (0 <= bytes)
		{
			return bytes;
		}

		if (MBEDTLS_ERR_SSL_WANT_READ  == bytes
		||  MBEDTLS_ERR_SSL_WANT_WRITE == bytes)
		{
			return 0;
		}

		return -1;
	}

} // namespace bnet

#endif // BNET_CONFIG_MBEDTLS

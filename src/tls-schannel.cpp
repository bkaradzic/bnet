/*
 * Copyright 2010-2026 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
 */

#include "bnet_p.h"

#if BNET_CONFIG_WINSCHANNEL

#define SECURITY_WIN32
#include <windows.h>
#include <sspi.h>
#include <schannel.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <ncrypt.h>

#include <bx/os.h>
#include <bx/file.h>
#include <bx/string.h>
#include <bx/allocator.h>

namespace bnet
{
	static const uint32_t kTlsBufferSize = 32<<10;

	static const DWORD kIscReq = 0
		| ISC_REQ_SEQUENCE_DETECT
		| ISC_REQ_REPLAY_DETECT
		| ISC_REQ_CONFIDENTIALITY
		| ISC_REQ_ALLOCATE_MEMORY
		| ISC_REQ_STREAM
		;

	static const DWORD kAscReq = 0
		| ASC_REQ_SEQUENCE_DETECT
		| ASC_REQ_REPLAY_DETECT
		| ASC_REQ_CONFIDENTIALITY
		| ASC_REQ_EXTENDED_ERROR
		| ASC_REQ_ALLOCATE_MEMORY
		| ASC_REQ_STREAM
		;

	static void*                   s_secur32      = NULL;
	static PSecurityFunctionTableA s_sspi         = NULL;
	static int32_t                 s_sspiRefCount = 0;

	static bool sspiInit()
	{
		if (NULL != s_sspi)
		{
			++s_sspiRefCount;
			return true;
		}

		s_secur32 = bx::dlopen("secur32.dll");
		if (NULL == s_secur32)
		{
			BX_TRACE("SChannel: failed to load secur32.dll.");
			return false;
		}

		INIT_SECURITY_INTERFACE_A initInterface = bx::dlsym<INIT_SECURITY_INTERFACE_A>(s_secur32, "InitSecurityInterfaceA");
		if (NULL != initInterface)
		{
			s_sspi = initInterface();
		}

		if (NULL == s_sspi)
		{
			BX_TRACE("SChannel: failed to resolve SSPI function table.");
			bx::dlclose(s_secur32);
			s_secur32 = NULL;
			return false;
		}

		++s_sspiRefCount;
		return true;
	}

	static void sspiShutdown()
	{
		if (0 != s_sspiRefCount
		&&  0 == --s_sspiRefCount)
		{
			s_sspi = NULL;

			if (NULL != s_secur32)
			{
				bx::dlclose(s_secur32);
				s_secur32 = NULL;
			}
		}
	}

	typedef BOOL (WINAPI* PFN_CryptStringToBinaryA)(LPCSTR, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
	typedef BOOL (WINAPI* PFN_CryptDecodeObjectEx)(DWORD, LPCSTR, const BYTE*, DWORD, DWORD, PCRYPT_DECODE_PARA, void*, DWORD*);
	typedef PCCERT_CONTEXT (WINAPI* PFN_CertCreateCertificateContext)(DWORD, const BYTE*, DWORD);
	typedef BOOL (WINAPI* PFN_CertFreeCertificateContext)(PCCERT_CONTEXT);
	typedef BOOL (WINAPI* PFN_CertSetCertificateContextProperty)(PCCERT_CONTEXT, DWORD, DWORD, const void*);

	typedef SECURITY_STATUS (WINAPI* PFN_NCryptOpenStorageProvider)(NCRYPT_PROV_HANDLE*, LPCWSTR, DWORD);
	typedef SECURITY_STATUS (WINAPI* PFN_NCryptImportKey)(NCRYPT_PROV_HANDLE, NCRYPT_KEY_HANDLE, LPCWSTR, NCryptBufferDesc*, NCRYPT_KEY_HANDLE*, PBYTE, DWORD, DWORD);
	typedef SECURITY_STATUS (WINAPI* PFN_NCryptFreeObject)(NCRYPT_HANDLE);
	typedef SECURITY_STATUS (WINAPI* PFN_NCryptDeleteKey)(NCRYPT_KEY_HANDLE, DWORD);

	struct Crypt
	{
		void* m_crypt32;
		void* m_ncrypt;

		PFN_CryptStringToBinaryA             CryptStringToBinaryA;
		PFN_CryptDecodeObjectEx              CryptDecodeObjectEx;
		PFN_CertCreateCertificateContext     CertCreateCertificateContext;
		PFN_CertFreeCertificateContext       CertFreeCertificateContext;
		PFN_CertSetCertificateContextProperty CertSetCertificateContextProperty;

		PFN_NCryptOpenStorageProvider        NCryptOpenStorageProvider;
		PFN_NCryptImportKey                  NCryptImportKey;
		PFN_NCryptFreeObject                 NCryptFreeObject;
		PFN_NCryptDeleteKey                  NCryptDeleteKey;
	};

	static Crypt s_crypt;
	static int32_t s_cryptRefCount = 0;

	static void cryptShutdown()
	{
		if (0 != s_cryptRefCount
		&&  0 == --s_cryptRefCount)
		{
			if (NULL != s_crypt.m_ncrypt)
			{
				bx::dlclose(s_crypt.m_ncrypt);
			}

			if (NULL != s_crypt.m_crypt32)
			{
				bx::dlclose(s_crypt.m_crypt32);
			}

			bx::memSet(&s_crypt, 0, sizeof(s_crypt) );
		}
	}

	static bool cryptInit()
	{
		if (0 != s_cryptRefCount)
		{
			++s_cryptRefCount;
			return true;
		}

		bx::memSet(&s_crypt, 0, sizeof(s_crypt) );
		++s_cryptRefCount;

		s_crypt.m_crypt32 = bx::dlopen("crypt32.dll");
		s_crypt.m_ncrypt  = bx::dlopen("ncrypt.dll");

		if (NULL != s_crypt.m_crypt32
		&&  NULL != s_crypt.m_ncrypt)
		{
			s_crypt.CryptStringToBinaryA              = bx::dlsym<PFN_CryptStringToBinaryA             >(s_crypt.m_crypt32, "CryptStringToBinaryA");
			s_crypt.CryptDecodeObjectEx               = bx::dlsym<PFN_CryptDecodeObjectEx              >(s_crypt.m_crypt32, "CryptDecodeObjectEx");
			s_crypt.CertCreateCertificateContext      = bx::dlsym<PFN_CertCreateCertificateContext     >(s_crypt.m_crypt32, "CertCreateCertificateContext");
			s_crypt.CertFreeCertificateContext        = bx::dlsym<PFN_CertFreeCertificateContext       >(s_crypt.m_crypt32, "CertFreeCertificateContext");
			s_crypt.CertSetCertificateContextProperty = bx::dlsym<PFN_CertSetCertificateContextProperty>(s_crypt.m_crypt32, "CertSetCertificateContextProperty");

			s_crypt.NCryptOpenStorageProvider         = bx::dlsym<PFN_NCryptOpenStorageProvider        >(s_crypt.m_ncrypt, "NCryptOpenStorageProvider");
			s_crypt.NCryptImportKey                   = bx::dlsym<PFN_NCryptImportKey                  >(s_crypt.m_ncrypt, "NCryptImportKey");
			s_crypt.NCryptFreeObject                  = bx::dlsym<PFN_NCryptFreeObject                 >(s_crypt.m_ncrypt, "NCryptFreeObject");
			s_crypt.NCryptDeleteKey                   = bx::dlsym<PFN_NCryptDeleteKey                  >(s_crypt.m_ncrypt, "NCryptDeleteKey");
		}

		if (NULL == s_crypt.CryptStringToBinaryA
		||  NULL == s_crypt.CryptDecodeObjectEx
		||  NULL == s_crypt.CertCreateCertificateContext
		||  NULL == s_crypt.CertFreeCertificateContext
		||  NULL == s_crypt.CertSetCertificateContextProperty
		||  NULL == s_crypt.NCryptOpenStorageProvider
		||  NULL == s_crypt.NCryptImportKey
		||  NULL == s_crypt.NCryptFreeObject
		||  NULL == s_crypt.NCryptDeleteKey)
		{
			BX_TRACE("SChannel: failed to resolve crypt32.dll/ncrypt.dll entry points.");
			cryptShutdown();
			return false;
		}

		return true;
	}

	struct TlsContext
	{
		CredHandle         m_cred;
		PCCERT_CONTEXT     m_cert;
		NCRYPT_PROV_HANDLE m_prov;
		NCRYPT_KEY_HANDLE  m_key;
		wchar_t            m_keyName[64];
		int32_t            m_refCount;
		bool               m_server;
	};

	struct TlsConnection
	{
		enum Enum
		{
			Init,
			Handshake,
			Established,
			Failed,
		};

		TlsContext* m_ctx;
		CredHandle* m_cred;
		SOCKET      m_socket;
		CtxtHandle  m_ctxt;
		bool        m_ctxtValid;
		bool        m_incomplete;
		bool        m_server;
		uint8_t     m_state;

		SecPkgContext_StreamSizes m_sizes;

		uint8_t*    m_in;
		uint32_t    m_inLen;
		uint32_t    m_inCap;

		uint8_t*    m_dec;
		uint32_t    m_decLen;
		uint32_t    m_decPos;
		uint32_t    m_decCap;

		char        m_host[256];
	};

	static bool tlsWouldBlock()
	{
		return WSAEWOULDBLOCK == WSAGetLastError();
	}

	static bool tlsSendAll(SOCKET _socket, const uint8_t* _data, uint32_t _len)
	{
		uint32_t offset = 0;
		while (offset < _len)
		{
			int bytes = ::send(_socket, (const char*)_data + offset, int(_len - offset), 0);
			if (0 < bytes)
			{
				offset += uint32_t(bytes);
			}
			else if (0 > bytes
				 &&  tlsWouldBlock() )
			{
				continue;
			}
			else
			{
				return false;
			}
		}

		return true;
	}

	static char* tlsLoadPem(const char* _pemOrPath)
	{
		if (!bx::strFind(bx::StringView(_pemOrPath), "-----BEGIN").isEmpty() )
		{
			const int32_t len = bx::strLen(_pemOrPath);
			char* out = (char*)bx::alloc(g_allocator, len+1);
			bx::memCopy(out, _pemOrPath, len);
			out[len] = '\0';
			return out;
		}

		bx::FileReader reader;
		bx::Error err;

		if (!reader.open(bx::FilePath(_pemOrPath), &err) )
		{
			BX_TRACE("SChannel: failed to open '%s'.", _pemOrPath);
			return NULL;
		}

		const int64_t size = reader.seek(0, bx::Whence::End);
		reader.seek(0, bx::Whence::Begin);

		char* out = (char*)bx::alloc(g_allocator, size+1);
		reader.read(out, int32_t(size), &err);
		reader.close();
		out[size] = '\0';

		if (!err.isOk() )
		{
			bx::free(g_allocator, out);
			return NULL;
		}

		return out;
	}

	static uint8_t* tlsPemToDer(const char* _pem, DWORD* _derSize)
	{
		DWORD size = 0;
		if (!s_crypt.CryptStringToBinaryA(_pem, 0, CRYPT_STRING_BASE64HEADER, NULL, &size, NULL, NULL) )
		{
			return NULL;
		}

		uint8_t* der = (uint8_t*)bx::alloc(g_allocator, size);
		if (!s_crypt.CryptStringToBinaryA(_pem, 0, CRYPT_STRING_BASE64HEADER, der, &size, NULL, NULL) )
		{
			bx::free(g_allocator, der);
			return NULL;
		}

		*_derSize = size;
		return der;
	}

	static void tlsMakeKeyName(wchar_t* _out, uint32_t _max)
	{
		static uint32_t s_counter = 0;

		const wchar_t* prefix = L"bnet-tls-";
		uint32_t pos = 0;

		for (; L'\0' != prefix[pos] && pos < _max-1; ++pos)
		{
			_out[pos] = prefix[pos];
		}

		const uint64_t id = ( (uint64_t)GetCurrentProcessId() << 32) | (uint64_t)(++s_counter + (uint32_t)GetTickCount64() );

		for (int32_t shift = 60; shift >= 0 && pos < _max-1; shift -= 4, ++pos)
		{
			_out[pos] = L"0123456789abcdef"[(id >> shift) & 0xf];
		}

		_out[pos] = L'\0';
	}

	static bool tlsImportKey(TlsContext* _ctx, const char* _keyPem)
	{
		DWORD derSize = 0;
		uint8_t* der = tlsPemToDer(_keyPem, &derSize);

		if (NULL == der)
		{
			BX_TRACE("SChannel: failed to decode private key PEM.");
			return false;
		}

		const uint8_t* rsaDer     = der;
		DWORD          rsaDerSize = derSize;

		CRYPT_PRIVATE_KEY_INFO* pki = NULL;

		if (bx::strFind(bx::StringView(_keyPem), "-----BEGIN RSA PRIVATE KEY").isEmpty() )
		{
			// PKCS#8 wrapped key, unwrap it first.
			DWORD pkiSize = 0;
			if (!s_crypt.CryptDecodeObjectEx(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING
				, PKCS_PRIVATE_KEY_INFO
				, der
				, derSize
				, CRYPT_DECODE_ALLOC_FLAG
				, NULL
				, &pki
				, &pkiSize
				) )
			{
				BX_TRACE("SChannel: unsupported private key format (RSA PKCS#1/PKCS#8 only).");
				bx::free(g_allocator, der);
				return false;
			}

			rsaDer     = pki->PrivateKey.pbData;
			rsaDerSize = pki->PrivateKey.cbData;
		}

		void* blob     = NULL;
		DWORD blobSize = 0;
		BOOL  decoded  = s_crypt.CryptDecodeObjectEx(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING
			, CNG_RSA_PRIVATE_KEY_BLOB
			, rsaDer
			, rsaDerSize
			, CRYPT_DECODE_ALLOC_FLAG
			, NULL
			, &blob
			, &blobSize
			);

		if (NULL != pki)
		{
			LocalFree(pki);
		}

		bx::free(g_allocator, der);

		if (!decoded)
		{
			BX_TRACE("SChannel: failed to decode RSA private key.");
			return false;
		}

		SECURITY_STATUS status = s_crypt.NCryptOpenStorageProvider(&_ctx->m_prov, MS_KEY_STORAGE_PROVIDER, 0);
		if (ERROR_SUCCESS == status)
		{
			tlsMakeKeyName(_ctx->m_keyName, BX_COUNTOF(_ctx->m_keyName) );

			NCryptBuffer buffer;
			buffer.BufferType = NCRYPTBUFFER_PKCS_KEY_NAME;
			buffer.cbBuffer   = (ULONG)( (wcslen(_ctx->m_keyName) + 1) * sizeof(wchar_t) );
			buffer.pvBuffer   = _ctx->m_keyName;

			NCryptBufferDesc desc;
			desc.ulVersion = NCRYPTBUFFER_VERSION;
			desc.cBuffers  = 1;
			desc.pBuffers  = &buffer;

			status = s_crypt.NCryptImportKey(_ctx->m_prov
				, 0
				, BCRYPT_RSAPRIVATE_BLOB
				, &desc
				, &_ctx->m_key
				, (PBYTE)blob
				, blobSize
				, NCRYPT_OVERWRITE_KEY_FLAG|NCRYPT_SILENT_FLAG
				);
		}

		LocalFree(blob);

		if (ERROR_SUCCESS != status)
		{
			BX_TRACE("SChannel: private key import failed 0x%08x.", status);
			return false;
		}

		CRYPT_KEY_PROV_INFO provInfo;
		bx::memSet(&provInfo, 0, sizeof(provInfo) );
		provInfo.pwszContainerName = _ctx->m_keyName;
		provInfo.pwszProvName      = (LPWSTR)MS_KEY_STORAGE_PROVIDER;
		provInfo.dwProvType        = 0;
		provInfo.dwKeySpec         = 0;

		if (!s_crypt.CertSetCertificateContextProperty(_ctx->m_cert
			, CERT_KEY_PROV_INFO_PROP_ID
			, 0
			, &provInfo
			) )
		{
			BX_TRACE("SChannel: failed to associate private key with certificate 0x%08x.", GetLastError() );
			return false;
		}

		return true;
	}

	static void tlsContextFree(TlsContext* _ctx)
	{
		if (SecIsValidHandle(&_ctx->m_cred) )
		{
			s_sspi->FreeCredentialsHandle(&_ctx->m_cred);
			SecInvalidateHandle(&_ctx->m_cred);
		}

		if (NULL != _ctx->m_cert)
		{
			s_crypt.CertFreeCertificateContext(_ctx->m_cert);
			_ctx->m_cert = NULL;
		}

		if (0 != _ctx->m_key)
		{
			// Deletes the persisted key and frees the handle.
			s_crypt.NCryptDeleteKey(_ctx->m_key, NCRYPT_SILENT_FLAG);
			_ctx->m_key = 0;
		}

		if (0 != _ctx->m_prov)
		{
			s_crypt.NCryptFreeObject(_ctx->m_prov);
			_ctx->m_prov = 0;
		}

		const bool server = _ctx->m_server;

		bx::deleteObject(g_allocator, _ctx);

		if (server)
		{
			cryptShutdown();
		}

		sspiShutdown();
	}

	TlsContext* tlsContextCreate()
	{
		if (!sspiInit() )
		{
			return NULL;
		}

		SCHANNEL_CRED cred;
		bx::memSet(&cred, 0, sizeof(cred) );
		cred.dwVersion = SCHANNEL_CRED_VERSION;
		cred.dwFlags   = 0
			| SCH_CRED_MANUAL_CRED_VALIDATION
			| SCH_CRED_NO_DEFAULT_CREDS
			| SCH_USE_STRONG_CRYPTO
			;

		TlsContext* ctx = BX_NEW(g_allocator, TlsContext);
		SecInvalidateHandle(&ctx->m_cred);
		ctx->m_cert     = NULL;
		ctx->m_prov     = 0;
		ctx->m_key      = 0;
		ctx->m_refCount = 1;
		ctx->m_server   = false;

		TimeStamp expiry;
		SECURITY_STATUS status = s_sspi->AcquireCredentialsHandleA(
			  NULL
			, const_cast<SEC_CHAR*>(UNISP_NAME_A)
			, SECPKG_CRED_OUTBOUND
			, NULL
			, &cred
			, NULL
			, NULL
			, &ctx->m_cred
			, &expiry
			);

		if (SEC_E_OK != status)
		{
			BX_TRACE("SChannel: AcquireCredentialsHandle failed 0x%08x.", status);
			SecInvalidateHandle(&ctx->m_cred);
			tlsContextFree(ctx);
			return NULL;
		}

		return ctx;
	}

	TlsContext* tlsServerContextCreate(const char* _cert, const char* _key)
	{
		if (NULL == _cert
		||  NULL == _key)
		{
			return NULL;
		}

		if (!sspiInit() )
		{
			return NULL;
		}

		if (!cryptInit() )
		{
			sspiShutdown();
			return NULL;
		}

		TlsContext* ctx = BX_NEW(g_allocator, TlsContext);
		SecInvalidateHandle(&ctx->m_cred);
		ctx->m_cert     = NULL;
		ctx->m_prov     = 0;
		ctx->m_key      = 0;
		ctx->m_refCount = 1;
		ctx->m_server   = true;

		char* certPem = tlsLoadPem(_cert);
		char* keyPem  = NULL == certPem ? NULL : tlsLoadPem(_key);

		bool ok = NULL != certPem && NULL != keyPem;

		if (ok)
		{
			DWORD certDerSize = 0;
			uint8_t* certDer = tlsPemToDer(certPem, &certDerSize);

			ok = NULL != certDer;

			if (ok)
			{
				ctx->m_cert = s_crypt.CertCreateCertificateContext(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING, certDer, certDerSize);
				bx::free(g_allocator, certDer);

				ok = NULL != ctx->m_cert;

				if (!ok)
				{
					BX_TRACE("SChannel: failed to create certificate context.");
				}
			}
			else
			{
				BX_TRACE("SChannel: failed to decode certificate PEM.");
			}
		}

		if (ok)
		{
			ok = tlsImportKey(ctx, keyPem);
		}

		if (NULL != certPem)
		{
			bx::free(g_allocator, certPem);
		}

		if (NULL != keyPem)
		{
			bx::free(g_allocator, keyPem);
		}

		if (ok)
		{
			SCHANNEL_CRED cred;
			bx::memSet(&cred, 0, sizeof(cred) );
			cred.dwVersion = SCHANNEL_CRED_VERSION;
			cred.cCreds    = 1;
			cred.paCred    = &ctx->m_cert;
			cred.dwFlags   = SCH_USE_STRONG_CRYPTO;

			TimeStamp expiry;
			SECURITY_STATUS status = s_sspi->AcquireCredentialsHandleA(
				  NULL
				, const_cast<SEC_CHAR*>(UNISP_NAME_A)
				, SECPKG_CRED_INBOUND
				, NULL
				, &cred
				, NULL
				, NULL
				, &ctx->m_cred
				, &expiry
				);

			if (SEC_E_OK != status)
			{
				BX_TRACE("SChannel: AcquireCredentialsHandle (inbound) failed 0x%08x.", status);
				SecInvalidateHandle(&ctx->m_cred);
				ok = false;
			}
		}

		if (!ok)
		{
			tlsContextFree(ctx);
			return NULL;
		}

		return ctx;
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

	static TlsConnection* tlsCreate(TlsContext* _ctx, SOCKET _socket, const char* _hostname, bool _server)
	{
		if (NULL == _ctx)
		{
			return NULL;
		}

		TlsConnection* tls = BX_NEW(g_allocator, TlsConnection);
		tls->m_ctx        = tlsContextAddRef(_ctx);
		tls->m_cred       = &_ctx->m_cred;
		tls->m_socket     = _socket;
		tls->m_ctxtValid  = false;
		tls->m_incomplete = false;
		tls->m_server     = _server;
		tls->m_state      = TlsConnection::Init;
		tls->m_in     = (uint8_t*)bx::alloc(g_allocator, kTlsBufferSize);
		tls->m_inLen  = 0;
		tls->m_inCap  = kTlsBufferSize;
		tls->m_dec    = (uint8_t*)bx::alloc(g_allocator, kTlsBufferSize);
		tls->m_decLen = 0;
		tls->m_decPos = 0;
		tls->m_decCap = kTlsBufferSize;

		if (NULL != _hostname)
		{
			bx::strCopy(tls->m_host, sizeof(tls->m_host), _hostname);
		}
		else
		{
			tls->m_host[0] = '\0';
		}

		return tls;
	}

	TlsConnection* tlsConnect(TlsContext* _ctx, SOCKET _socket, const char* _hostname)
	{
		return tlsCreate(_ctx, _socket, _hostname, false);
	}

	TlsConnection* tlsAccept(TlsContext* _ctx, SOCKET _socket)
	{
		return tlsCreate(_ctx, _socket, NULL, true);
	}

	void tlsDestroy(TlsConnection* _tls)
	{
		if (NULL == _tls)
		{
			return;
		}

		if (_tls->m_ctxtValid)
		{
			s_sspi->DeleteSecurityContext(&_tls->m_ctxt);
		}

		bx::free(g_allocator, _tls->m_in);
		bx::free(g_allocator, _tls->m_dec);

		TlsContext* ctx = _tls->m_ctx;
		bx::deleteObject(g_allocator, _tls);
		tlsContextDestroy(ctx);
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

		SEC_CHAR* target = '\0' != _tls->m_host[0] ? _tls->m_host : NULL;

		if (TlsConnection::Init == _tls->m_state)
		{
			if (_tls->m_server)
			{
				_tls->m_state = TlsConnection::Handshake;
			}
			else
			{
				SecBuffer outBuf = { 0, SECBUFFER_TOKEN, NULL };
				SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, &outBuf };
				DWORD outFlags = 0;
				TimeStamp expiry;

				SECURITY_STATUS status = s_sspi->InitializeSecurityContextA(
					  _tls->m_cred
					, NULL
					, target
					, kIscReq
					, 0
					, 0
					, NULL
					, 0
					, &_tls->m_ctxt
					, &outDesc
					, &outFlags
					, &expiry
					);

				_tls->m_ctxtValid = true;

				if (SEC_I_CONTINUE_NEEDED != status)
				{
					BX_TRACE("SChannel: initial InitializeSecurityContext failed 0x%08x.", status);
					_tls->m_state = TlsConnection::Failed;
					return -1;
				}

				if (0 != outBuf.cbBuffer
				&&  NULL != outBuf.pvBuffer)
				{
					bool ok = tlsSendAll(_tls->m_socket, (uint8_t*)outBuf.pvBuffer, outBuf.cbBuffer);
					s_sspi->FreeContextBuffer(outBuf.pvBuffer);

					if (!ok)
					{
						_tls->m_state = TlsConnection::Failed;
						return -1;
					}
				}

				_tls->m_state = TlsConnection::Handshake;
				return 0;
			}
		}

		for (;;)
		{
			const bool needRead = 0 == _tls->m_inLen || _tls->m_incomplete;
			if (needRead)
			{
				if (_tls->m_inLen >= _tls->m_inCap)
				{
					BX_TRACE("SChannel: handshake record exceeds buffer.");
					_tls->m_state = TlsConnection::Failed;
					return -1;
				}

				int n = ::recv(_tls->m_socket
					, (char*)_tls->m_in + _tls->m_inLen
					, int(_tls->m_inCap - _tls->m_inLen)
					, 0
					);

				if (0 == n)
				{
					_tls->m_state = TlsConnection::Failed;
					return -1;
				}

				if (0 > n)
				{
					if (tlsWouldBlock() )
					{
						return 0;
					}

					_tls->m_state = TlsConnection::Failed;
					return -1;
				}

				_tls->m_inLen += uint32_t(n);
				_tls->m_incomplete = false;
			}

			SecBuffer inBuf[2];
			inBuf[0].cbBuffer   = _tls->m_inLen;
			inBuf[0].BufferType = SECBUFFER_TOKEN;
			inBuf[0].pvBuffer   = _tls->m_in;
			inBuf[1].cbBuffer   = 0;
			inBuf[1].BufferType = SECBUFFER_EMPTY;
			inBuf[1].pvBuffer   = NULL;
			SecBufferDesc inDesc = { SECBUFFER_VERSION, 2, inBuf };

			SecBuffer outBuf = { 0, SECBUFFER_TOKEN, NULL };
			SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, &outBuf };
			DWORD outFlags = 0;
			TimeStamp expiry;

			SECURITY_STATUS status;

			if (_tls->m_server)
			{
				status = s_sspi->AcceptSecurityContext(
					  _tls->m_cred
					, _tls->m_ctxtValid ? &_tls->m_ctxt : NULL
					, &inDesc
					, kAscReq
					, 0
					, &_tls->m_ctxt
					, &outDesc
					, &outFlags
					, &expiry
					);

				if (SEC_E_INCOMPLETE_MESSAGE != status
				&&  !FAILED(status) )
				{
					_tls->m_ctxtValid = true;
				}
			}
			else
			{
				status = s_sspi->InitializeSecurityContextA(
					  _tls->m_cred
					, &_tls->m_ctxt
					, target
					, kIscReq
					, 0
					, 0
					, &inDesc
					, 0
					, NULL
					, &outDesc
					, &outFlags
					, &expiry
					);
			}

			if (0 != outBuf.cbBuffer
			&&  NULL != outBuf.pvBuffer)
			{
				bool ok = tlsSendAll(_tls->m_socket, (uint8_t*)outBuf.pvBuffer, outBuf.cbBuffer);
				s_sspi->FreeContextBuffer(outBuf.pvBuffer);

				if (!ok)
				{
					_tls->m_state = TlsConnection::Failed;
					return -1;
				}
			}

			if (SEC_E_INCOMPLETE_MESSAGE == status)
			{
				_tls->m_incomplete = true;
				continue;
			}

			if (SECBUFFER_EXTRA == inBuf[1].BufferType)
			{
				uint32_t extra = inBuf[1].cbBuffer;
				bx::memMove(_tls->m_in, _tls->m_in + (_tls->m_inLen - extra), extra);
				_tls->m_inLen = extra;
			}
			else
			{
				_tls->m_inLen = 0;
			}

			if (SEC_I_CONTINUE_NEEDED == status)
			{
				continue;
			}

			if (SEC_E_OK == status)
			{
				s_sspi->QueryContextAttributesA(&_tls->m_ctxt, SECPKG_ATTR_STREAM_SIZES, &_tls->m_sizes);
				_tls->m_state = TlsConnection::Established;
				return 1;
			}

			BX_TRACE("SChannel: %s failed 0x%08x."
				, _tls->m_server ? "AcceptSecurityContext" : "InitializeSecurityContext"
				, status
				);
			_tls->m_state = TlsConnection::Failed;
			return -1;
		}
	}

	static int tlsServeDecrypted(TlsConnection* _tls, char* _data, int _len)
	{
		const uint32_t avail = _tls->m_decLen - _tls->m_decPos;
		const uint32_t num   = avail < uint32_t(_len) ? avail : uint32_t(_len);

		bx::memCopy(_data, _tls->m_dec + _tls->m_decPos, num);

		_tls->m_decPos += num;

		if (_tls->m_decPos == _tls->m_decLen)
		{
			_tls->m_decPos = 0;
			_tls->m_decLen = 0;
		}

		return int(num);
	}

	int tlsRecv(TlsConnection* _tls, char* _data, int _len)
	{
		if (TlsConnection::Established != _tls->m_state)
		{
			WSASetLastError(WSAEWOULDBLOCK);
			return -1;
		}

		if (_tls->m_decPos < _tls->m_decLen)
		{
			return tlsServeDecrypted(_tls, _data, _len);
		}

		for (;;)
		{
			if (0 < _tls->m_inLen)
			{
				SecBuffer bufs[4];
				bufs[0].cbBuffer   = _tls->m_inLen;
				bufs[0].BufferType = SECBUFFER_DATA;
				bufs[0].pvBuffer   = _tls->m_in;
				for (int ii = 1; ii < 4; ++ii)
				{
					bufs[ii].cbBuffer   = 0;
					bufs[ii].BufferType = SECBUFFER_EMPTY;
					bufs[ii].pvBuffer   = NULL;
				}
				SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs };

				SECURITY_STATUS status = s_sspi->DecryptMessage(&_tls->m_ctxt, &desc, 0, NULL);

				if (SEC_E_OK == status)
				{
					SecBuffer* pData  = NULL;
					SecBuffer* pExtra = NULL;
					for (int ii = 1; ii < 4; ++ii)
					{
						if (NULL == pData
						&&  SECBUFFER_DATA == bufs[ii].BufferType)
						{
							pData = &bufs[ii];
						}

						if (NULL == pExtra
						&&  SECBUFFER_EXTRA == bufs[ii].BufferType)
						{
							pExtra = &bufs[ii];
						}
					}

					if (NULL != pData
					&&  0 < pData->cbBuffer)
					{
						uint32_t plen = pData->cbBuffer < _tls->m_decCap ? pData->cbBuffer : _tls->m_decCap;
						bx::memCopy(_tls->m_dec, pData->pvBuffer, plen);
						_tls->m_decLen = plen;
						_tls->m_decPos = 0;
					}

					if (NULL != pExtra
					&&  0 < pExtra->cbBuffer)
					{
						uint32_t extra = pExtra->cbBuffer;
						bx::memMove(_tls->m_in, pExtra->pvBuffer, extra);
						_tls->m_inLen = extra;
					}
					else
					{
						_tls->m_inLen = 0;
					}

					if (0 < _tls->m_decLen)
					{
						return tlsServeDecrypted(_tls, _data, _len);
					}

					continue;
				}
				else if (SEC_E_INCOMPLETE_MESSAGE == status)
				{
					// Need a complete record; fall through to read more.
				}
				else if (SEC_I_CONTEXT_EXPIRED == status)
				{
					return 0;
				}
				else if (SEC_I_RENEGOTIATE == status)
				{
					BX_TRACE("SChannel: renegotiation requested (unsupported).");
					WSASetLastError(WSAECONNRESET);
					return -1;
				}
				else
				{
					BX_TRACE("SChannel: DecryptMessage failed 0x%08x.", status);
					WSASetLastError(WSAECONNRESET);
					return -1;
				}
			}

			if (_tls->m_inLen >= _tls->m_inCap)
			{
				BX_TRACE("SChannel: receive record exceeds buffer.");
				WSASetLastError(WSAECONNRESET);
				return -1;
			}

			int n = ::recv(_tls->m_socket
				, (char*)_tls->m_in + _tls->m_inLen
				, int(_tls->m_inCap - _tls->m_inLen)
				, 0
				);

			if (0 == n)
			{
				return 0;
			}

			if (0 > n)
			{
				// Preserve WSAEWOULDBLOCK / error for the caller.
				return -1;
			}

			_tls->m_inLen += uint32_t(n);
		}
	}

	int tlsSend(TlsConnection* _tls, const char* _data, int _len)
	{
		if (TlsConnection::Established != _tls->m_state)
		{
			return -1;
		}

		const uint32_t header  = _tls->m_sizes.cbHeader;
		const uint32_t trailer = _tls->m_sizes.cbTrailer;
		const uint32_t maxMsg  = _tls->m_sizes.cbMaximumMessage;

		const uint32_t total = uint32_t(_len);
		uint32_t offset = 0;

		uint8_t* scratch = (uint8_t*)bx::alloc(g_allocator, header + maxMsg + trailer);

		while (offset < total)
		{
			uint32_t chunk = total - offset;
			if (chunk > maxMsg)
			{
				chunk = maxMsg;
			}

			bx::memCopy(scratch + header, _data + offset, chunk);

			SecBuffer bufs[3];
			bufs[0].cbBuffer   = header;
			bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
			bufs[0].pvBuffer   = scratch;
			bufs[1].cbBuffer   = chunk;
			bufs[1].BufferType = SECBUFFER_DATA;
			bufs[1].pvBuffer   = scratch + header;
			bufs[2].cbBuffer   = trailer;
			bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
			bufs[2].pvBuffer   = scratch + header + chunk;
			SecBufferDesc desc = { SECBUFFER_VERSION, 3, bufs };

			SECURITY_STATUS status = s_sspi->EncryptMessage(&_tls->m_ctxt, 0, &desc, 0);

			if (SEC_E_OK != status)
			{
				BX_TRACE("SChannel: EncryptMessage failed 0x%08x.", status);
				bx::free(g_allocator, scratch);
				return -1;
			}

			uint32_t encLen = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
			if (!tlsSendAll(_tls->m_socket, scratch, encLen) )
			{
				bx::free(g_allocator, scratch);
				return -1;
			}

			offset += chunk;
		}

		bx::free(g_allocator, scratch);
		return int(total);
	}

} // namespace bnet

#endif // BNET_CONFIG_WINSCHANNEL

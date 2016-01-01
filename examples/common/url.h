/*
 * Copyright 2011-2016 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bnet#license-bsd-2-clause
 */

#ifndef __URL_H__
#define __URL_H__

struct UrlToken
{
	enum Enum
	{
		Scheme,
		UserName,
		Password,
		Host,
		Port,
		Path,
		Query,
		Fragment,

		Count
	};
};

void tokenizeUrl(const char* _url, char* _buf, size_t _bufSize, char* _tokens[UrlToken::Count]);
void urlEncode(const char* _str, char* _buf, size_t _bufSize);

#endif // __URL_H__

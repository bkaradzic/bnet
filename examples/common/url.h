/*
 * Copyright 2011-2012 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
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

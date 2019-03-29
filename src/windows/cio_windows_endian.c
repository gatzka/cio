/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2018> <Stephan Gatzka>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include<windows.h>

#include "cio_endian.h"
#if REG_DWORD == REG_DWORD_LITTLE_ENDIAN

uint16_t cio_be16toh(uint16_t big_endian_16bits)
{
	return _byteswap_ushort(big_endian_16bits);
}

uint64_t cio_be64toh(uint64_t big_endian_64bits)
{
	return _byteswap_uint64(big_endian_64bits);
}

uint16_t cio_htobe16(uint16_t host_endian_16bits)
{
	return _byteswap_ushort(host_endian_16bits);
}

uint64_t cio_htobe64(uint64_t host_endian_64bits)
{
	return _byteswap_uint64(host_endian_64bits);
}
#else
uint16_t cio_be16toh(uint16_t big_endian_16bits)
{
	return big_endian_16bits;
}

uint64_t cio_be64toh(uint64_t big_endian_64bits)
{
	return big_endian_64bits;
}

uint16_t cio_htobe16(uint16_t host_endian_16bits)
{
	return host_endian_16bits;
}

uint64_t cio_htobe64(uint64_t host_endian_64bits)
{
	return host_endian_64bits;
}
#endif



/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2020> <Stephan Gatzka>
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

#ifndef CIO_ADDRESS_FAMILY_H
#define CIO_ADDRESS_FAMILY_H

#include "cio_address_family_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cio_address_family {
	CIO_ADDRESS_FAMILY_UNSPEC = CIO_ADDRESS_FAMILY_UNSPEC_IMPL,
	CIO_ADDRESS_FAMILY_INET4 = CIO_ADDRESS_FAMILY_INET4_IMPL,
	CIO_ADDRESS_FAMILY_INET6 = CIO_ADDRESS_FAMILY_INET6_IMPL,
	CIO_ADDRESS_FAMILY_UNIX = CIO_ADDRESS_FAMILY_UNIX_IMPL
};

#ifdef __cplusplus
}
#endif

#endif

/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2017> <Stephan Gatzka>
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

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio/compiler.h"
#include "cio/error_code.h"
#include "cio/random.h"

enum cio_error cio_entropy_get_bytes(void *bytes, size_t num_bytes)
{
	FILE *dev_urandom = fopen("/dev/urandom", "re");
	if (cio_unlikely(dev_urandom == NULL)) {
		return (enum cio_error)(-errno);
	}

	enum cio_error err = CIO_SUCCESS;
	size_t ret = fread_unlocked(bytes, (size_t)1, num_bytes, dev_urandom);
	if (cio_unlikely(ret < num_bytes)) {
		err = CIO_EOF;
	}

	int close_ret = fclose(dev_urandom);
	(void)close_ret;

	return err;
}

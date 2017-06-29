/*
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

#include <stdlib.h>

#include "cio_buffer_allocator.h"
#include "cio_linux_alloc.h"

void *cio_malloc(size_t size)
{
	return malloc(size);
}

void cio_free(void *ptr)
{
	free(ptr);
}

static struct cio_buffer allocate(struct cio_buffer_allocator *context, size_t size)
{
	(void)context;
	struct cio_buffer buffer;
	buffer.address = malloc(size);
	buffer.size = size;
	return buffer;
}

static void free_mem(struct cio_buffer_allocator *context, void *ptr)
{
	(void)context;
	free(ptr);
}

static struct cio_buffer_allocator linux_system_allocator = {
	.alloc = allocate,
	.free = free_mem
};


struct cio_buffer_allocator *get_system_allocator(void)
{
	return &linux_system_allocator;
}

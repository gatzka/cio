/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2019> <Stephan Gatzka>
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

#include "fff.h"
#include "unity.h"

#include "cio_error_code.h"
#include "cio_http_location.h"
#include "cio_http_location_handler.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

struct dummy_handler {
	struct cio_http_location_handler handler;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;
};

DEFINE_FFF_GLOBALS

static struct cio_http_location_handler *alloc_dummy_handler(const void *config)
{
	(void)config;
	struct dummy_handler *handler = malloc(sizeof(*handler));
	if (cio_unlikely(handler == NULL)) {
		return NULL;
	} else {
		cio_http_location_handler_init(&handler->handler);
		cio_write_buffer_head_init(&handler->wbh);
		return &handler->handler;
	}
}

void setUp(void)
{
	FFF_RESET_HISTORY();
}

void tearDown(void)
{
}

static void test_request_target_init(void)
{
	struct location_init_arguments {
		struct cio_http_location *target;
		const char *path;
		cio_http_alloc_handler handler;
		enum cio_error expected_result;
	};

	struct cio_http_location target;

	struct location_init_arguments args[] = {
		{.target = &target, .path = "/foo", .handler = alloc_dummy_handler, .expected_result = CIO_SUCCESS},
		{.target = NULL, .path = "/foo", .handler = alloc_dummy_handler, .expected_result = CIO_INVALID_ARGUMENT},
		{.target = &target, .path = NULL, .handler = alloc_dummy_handler, .expected_result = CIO_INVALID_ARGUMENT},
		{.target = &target, .path = "/foo", .handler = NULL, .expected_result = CIO_INVALID_ARGUMENT},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(args); i++) {
		struct location_init_arguments arg = args[i];
		enum cio_error err = cio_http_location_init(arg.target, arg.path, NULL, arg.handler);
		TEST_ASSERT_EQUAL_MESSAGE(arg.expected_result, err, "Initialization failed!");
	}
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_request_target_init);
	return UNITY_END();
}

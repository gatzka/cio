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

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio/error_code.h"
#include "cio/eventloop.h"
#include "cio/timer.h"

static const uint64_t FIVE_S = 5000000000;
static uint_fast8_t expirations = 0;
static const uint_fast8_t MAX_EXPIRATIONS = 5;

static struct cio_eventloop loop;

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void handle_timeout(struct cio_timer *timer, void *handler_context, enum cio_error err)
{
	(void)handler_context;
	if (err == CIO_SUCCESS) {
		(void)fprintf(stdout, "timer expired!\n");
		if (expirations++ < MAX_EXPIRATIONS) {
			if (cio_timer_expires_from_now(timer, FIVE_S, handle_timeout, NULL) != CIO_SUCCESS) {
				(void)fprintf(stderr, "arming timer failed!\n");
			}
		} else {
			cio_timer_close(timer);
			cio_eventloop_cancel(&loop);
		}
	} else {
		(void)fprintf(stderr, "timer error!\n");
	}
}

int main(void)
{
	int ret = EXIT_SUCCESS;

	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return -1;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		(void)signal(SIGTERM, SIG_DFL);
		return -1;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != CIO_SUCCESS) {
		return EXIT_FAILURE;
	}

	struct cio_timer timer;
	err = cio_timer_init(&timer, &loop, NULL);
	if (err != CIO_SUCCESS) {
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	if (cio_timer_expires_from_now(&timer, FIVE_S, handle_timeout, NULL) != CIO_SUCCESS) {
		(void)fprintf(stderr, "arming timer failed!\n");
		ret = EXIT_FAILURE;
		goto destroy_loop;
	}

	err = cio_eventloop_run(&loop);
	if (err != CIO_SUCCESS) {
		(void)fprintf(stderr, "error in cio_eventloop_run!\n");
	}

destroy_loop:
	cio_eventloop_destroy(&loop);

	return ret;
}

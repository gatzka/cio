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

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "fff.h"
#include "unity.h"

#include "cio_error_code.h"
#include "cio_linux_socket.h"
#include "cio_linux_socket_utils.h"
#include "cio_read_buffer.h"
#include "cio_write_buffer.h"

#undef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_write, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, struct cio_eventloop *, const struct cio_event_notifier *)

FAKE_VALUE_FUNC(int, close, int)
FAKE_VALUE_FUNC(ssize_t, read, int, void *, size_t)
FAKE_VALUE_FUNC(ssize_t, sendmsg, int, const struct msghdr *, int)
FAKE_VALUE_FUNC(int, setsockopt, int, int, int, const void *, socklen_t)

FAKE_VALUE_FUNC0(int, cio_linux_socket_create)

void on_close(struct cio_socket *s);
FAKE_VOID_FUNC(on_close, struct cio_socket *)

void read_handler(struct cio_io_stream *context, void *handler_context, enum cio_error err, struct cio_read_buffer *buffer);
FAKE_VOID_FUNC(read_handler, struct cio_io_stream *, void *, enum cio_error, struct cio_read_buffer *)

void write_handler(struct cio_io_stream *stream, void *handler_context, const struct cio_write_buffer *, enum cio_error err, size_t bytes_transferred);
FAKE_VOID_FUNC(write_handler, struct cio_io_stream *, void *, const struct cio_write_buffer *, enum cio_error, size_t)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static uint8_t read_buffer[100];
static size_t available_read_data;
static uint8_t readback_buffer[200];
static size_t bytes_to_send;
static uint8_t send_buffer[200];

static struct cio_eventloop loop;

static int socket_create_fails(void)
{
	errno = EINVAL;
	return -1;
}

static ssize_t read_ok(int fd, void *buf, size_t count)
{
	(void)fd;
	(void)buf;
	(void)count;

	memcpy(buf, read_buffer, available_read_data);
	return available_read_data;
}

static ssize_t read_blocks(int fd, void *buf, size_t count)
{
	(void)fd;
	(void)buf;
	(void)count;

	errno = EWOULDBLOCK;
	return -1;
}

static ssize_t read_fails(int fd, void *buf, size_t count)
{
	(void)fd;
	(void)buf;
	(void)count;

	errno = EINVAL;
	return -1;
}

static ssize_t send_all(int fd, const struct msghdr *msg, int flags)
{
	(void)fd;
	(void)flags;
	ssize_t len = 0;
	for (unsigned int i = 0; i < msg->msg_iovlen; i++) {
		memcpy(&send_buffer[len], msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
		len += msg->msg_iov[i].iov_len;
	}

	return len;
}

static ssize_t send_blocks(int fd, const struct msghdr *msg, int flags)
{
	(void)fd;
	(void)msg;
	(void)flags;
	errno = EWOULDBLOCK;
	return -1;
}

static ssize_t send_parts(int fd, const struct msghdr *msg, int flags)
{
	(void)fd;
	(void)flags;
	unsigned int len = 0;
	size_t remaining_bytes = bytes_to_send;
	for (unsigned int i = 0; i < msg->msg_iovlen; i++) {
		size_t minimum = MIN(remaining_bytes, msg->msg_iov[i].iov_len);
		memcpy(&send_buffer[len], msg->msg_iov[i].iov_base, minimum);
		len += minimum;
		remaining_bytes -= minimum;
		if (remaining_bytes == 0) {
			return bytes_to_send;
		}
	}

	return bytes_to_send;
}

static ssize_t send_fails(int fd, const struct msghdr *msg, int flags)
{
	(void)fd;
	(void)msg;
	(void)flags;
	errno = EINVAL;
	return -1;
}

static int setsockopt_fails(int fd, int level, int option_name,
                            const void *option_value, socklen_t option_len)
{
	(void)fd;
	(void)level;
	(void)option_name;
	(void)option_value;
	(void)option_len;

	errno = EINVAL;
	return -1;
}

static int setsockopt_ok(int fd, int level, int option_name,
                         const void *option_value, socklen_t option_len)
{
	(void)fd;
	(void)level;
	(void)option_name;
	(void)option_value;
	(void)option_len;

	return 0;
}

void setUp(void)
{
	FFF_RESET_HISTORY();

	RESET_FAKE(cio_linux_eventloop_add);
	RESET_FAKE(cio_linux_eventloop_remove);
	RESET_FAKE(cio_linux_eventloop_register_read);
	RESET_FAKE(cio_linux_eventloop_register_write);

	RESET_FAKE(close);
	RESET_FAKE(read);
	RESET_FAKE(sendmsg);
	RESET_FAKE(setsockopt);

	RESET_FAKE(cio_linux_socket_create);
	RESET_FAKE(read_handler);
	RESET_FAKE(write_handler);
	RESET_FAKE(on_close);

	memset(read_buffer, 0xff, sizeof(read_buffer));
	memset(send_buffer, 0xff, sizeof(send_buffer));
	available_read_data = 0;
	bytes_to_send = 0;
}

static void test_socket_init(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value if cio_socket_init() not correct!");
}

static void test_socket_init_socket_create_fails(void)
{
	struct cio_socket s;
	cio_linux_socket_create_fake.custom_fake = socket_create_fails;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init() not correct!");
}

static void test_socket_init_socket_create_no_socket(void)
{
	enum cio_error err = cio_socket_init(NULL, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value of cio_socket_init() not correct!");
}

static void test_socket_init_socket_create_no_loop(void)
{
	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value of cio_socket_init() not correct!");
}

static void test_socket_init_eventloop_add_fails(void)
{
	struct cio_socket s;
	cio_linux_eventloop_add_fake.return_val = CIO_INVALID_ARGUMENT;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init() not correct!");
}

static void test_socket_close_without_hook(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init() not correct!");

	err = s.close(&s);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of close() not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Socket close was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(s.ev.fd, close_fake.arg0_val, "Socket close was not called with correct parameter!");
}

static void test_socket_close_with_hook(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init() not correct!");

	err = s.close(&s);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value if close() not correct!");
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, close_fake.arg0_val);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&s, on_close_fake.arg0_val);
}

static void test_socket_close_no_stream(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	err = s.close(NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value of close() not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, close_fake.call_count, "Close handler was called!");
}

static void test_socket_enable_nodelay(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	err = s.set_tcp_no_delay(&s, true);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of set_tcp_no_delay not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(s.ev.fd, setsockopt_fake.arg0_val, "fd for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(IPPROTO_TCP, setsockopt_fake.arg1_val, "level for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(TCP_NODELAY, setsockopt_fake.arg2_val, "option name for setsockopt not correct!");
}

static void test_socket_disable_nodelay(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	err = s.set_tcp_no_delay(&s, false);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of set_tcp_no_delay not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(s.ev.fd, setsockopt_fake.arg0_val, "fd for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(IPPROTO_TCP, setsockopt_fake.arg1_val, "level for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(TCP_NODELAY, setsockopt_fake.arg2_val, "option name for setsockopt not correct!");
}

static void test_socket_nodelay_setsockopt_fails(void)
{
	struct cio_socket s;

	setsockopt_fake.custom_fake = setsockopt_fails;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	err = s.set_tcp_no_delay(&s, false);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of set_tcp_no_delay not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(s.ev.fd, setsockopt_fake.arg0_val, "fd for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(IPPROTO_TCP, setsockopt_fake.arg1_val, "level for setsockopt not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(TCP_NODELAY, setsockopt_fake.arg2_val, "option name for setsockopt not correct!");
}

static void test_socket_enable_keepalive(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of set_keep_alive not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(4, setsockopt_fake.call_count, "setsockopt was not called 4 times!");
}

static void test_socket_disable_keepalive(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	err = s.set_keep_alive(&s, false, 10, 9, 8);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of set_keep_alive not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called 1 time!");
}

static void test_socket_disable_keepalive_setsockopt_fails(void)
{
	struct cio_socket s;
	int (*custom_fakes[])(int, int, int, const void *, socklen_t) =
	    {
	        setsockopt_fails,
	    };

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, false, 10, 9, 8);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value for failing set_keep_alive not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called 1 time!");
}

static void test_socket_enable_keepalive_keep_idle_fails(void)
{
	struct cio_socket s;
	int (*custom_fakes[])(int, int, int, const void *, socklen_t) =
	    {
	        setsockopt_fails,
	        setsockopt_ok,
	        setsockopt_ok,
	        setsockopt_ok};

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value for failing set_keep_alive not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, setsockopt_fake.call_count, "setsockopt was not called 1 time!");
}

static void test_socket_enable_keepalive_keep_intvl_fails(void)
{
	struct cio_socket s;
	int (*custom_fakes[])(int, int, int, const void *, socklen_t) =
	    {
	        setsockopt_ok,
	        setsockopt_fails,
	        setsockopt_ok,
	        setsockopt_ok};

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value for failing set_keep_alive not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(2, setsockopt_fake.call_count, "setsockopt was not called 2 times!");
}

static void test_socket_enable_keepalive_keep_cnt(void)
{
	struct cio_socket s;
	int (*custom_fakes[])(int, int, int, const void *, socklen_t) =
	    {
	        setsockopt_ok,
	        setsockopt_ok,
	        setsockopt_fails,
	        setsockopt_ok};

	enum cio_error err = cio_socket_init(&s, &loop, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value for failing set_keep_alive not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(3, setsockopt_fake.call_count, "setsockopt was not called 3 times!");
}

static void test_socket_stream_close(void)
{
	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	struct cio_io_stream *stream = s.get_io_stream(&s);
	err = stream->close(stream);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of stream close() not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, close_fake.call_count, "Close was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(s.ev.fd, close_fake.arg0_val, "file descriptor fo close() not correct!");
}

static void test_socket_readsome(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_ok;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	struct cio_read_buffer rb;
	cio_read_buffer_init(&rb, readback_buffer, sizeof(readback_buffer));
	struct cio_io_stream *stream = s.get_io_stream(&s);
	err = stream->read_some(stream, &rb, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	s.ev.read_callback(s.ev.context);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "read handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(stream, read_handler_fake.arg0_val, "First parameter for read handler is not the stream!");
	TEST_ASSERT_EQUAL_MESSAGE(NULL, read_handler_fake.arg1_val, "Context parameter for read handler is not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, read_handler_fake.arg2_val, "Read handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, read_handler_fake.arg3_val, "Original buffer was not passed to read handler!");
	TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(read_buffer, readback_buffer, data_to_read), "Content of data passed to read handler is not correct!");
}

static void test_socket_readsome_register_read_fails(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_ok;
	cio_linux_eventloop_register_read_fake.return_val = CIO_INVALID_ARGUMENT;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	struct cio_read_buffer rb;
	cio_read_buffer_init(&rb, readback_buffer, sizeof(readback_buffer));
	struct cio_io_stream *stream = s.get_io_stream(&s);
	err = stream->read_some(stream, &rb, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.call_count, "Handler was called!");
}

static void test_socket_readsome_read_blocks(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_blocks;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	struct cio_read_buffer rb;
	cio_read_buffer_init(&rb, readback_buffer, sizeof(readback_buffer));
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->read_some(stream, &rb, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.call_count, "Handler was called!");
}

static void test_socket_readsome_read_fails(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_fails;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	struct cio_read_buffer rb;
	cio_read_buffer_init(&rb, readback_buffer, sizeof(readback_buffer));
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->read_some(stream, &rb, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	s.ev.read_callback(s.ev.context);

	TEST_ASSERT_EQUAL_MESSAGE(1, read_handler_fake.call_count, "Read handler was not called!");
	TEST_ASSERT_EQUAL_MESSAGE(stream, read_handler_fake.arg0_val, "Original stream was not passed to read handler!");
	TEST_ASSERT_EQUAL_MESSAGE(NULL, read_handler_fake.arg1_val, "Context parameter for read handler is not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, read_handler_fake.arg2_val, "Read handler was called with CIO_SUCCESS for a failing read!");
	TEST_ASSERT_EQUAL_MESSAGE(&rb, read_handler_fake.arg3_val, "Original buffer was not passed to read handler!");
}

static void test_socket_readsome_no_stream(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_fails;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	struct cio_read_buffer rb;
	cio_read_buffer_init(&rb, readback_buffer, sizeof(readback_buffer));
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->read_some(NULL, &rb, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.call_count, "Handler was called!");
}

static void test_socket_readsome_no_buffer(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_fails;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->read_some(stream, NULL, read_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.call_count, "Handler was called!");
}

static void test_socket_readsome_no_handler(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_fails;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");

	struct cio_read_buffer rb;
	cio_read_buffer_init(&rb, readback_buffer, sizeof(readback_buffer));
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->read_some(stream, &rb, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, read_handler_fake.call_count, "Handler was called!");
}

static void test_socket_writesome_all(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	sendmsg_fake.custom_fake = send_all;

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(stream, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write_handler was not called exactly once!");
	TEST_ASSERT_EQUAL_MESSAGE(stream, write_handler_fake.arg0_val, "write_handler was not called with correct stream!");
	TEST_ASSERT_EQUAL_MESSAGE(NULL, write_handler_fake.arg1_val, "write_handler was not called with correct handler_context!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, write_handler_fake.arg2_val, "write_handler was not called with original buffer!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg3_val, "write_handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(buffer), write_handler_fake.arg4_val, "write_handler was not called with the correct number of bytes written!");
	TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(send_buffer, buffer, sizeof(buffer)), "Buffer was not sent correctly!");
}

static void test_socket_writesome_parts(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	bytes_to_send = 9;
	sendmsg_fake.custom_fake = send_parts;

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	wb.data.element.const_data = buffer;
	wb.data.element.length = sizeof(buffer);
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(stream, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write_handler was not called exactly once!");
	TEST_ASSERT_EQUAL_MESSAGE(stream, write_handler_fake.arg0_val, "write_handler was not called with correct stream!");
	TEST_ASSERT_EQUAL_MESSAGE(NULL, write_handler_fake.arg1_val, "write_handler was not called with correct handler_context!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, write_handler_fake.arg2_val, "write_handler was not called with original buffer!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg3_val, "write_handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(bytes_to_send, write_handler_fake.arg4_val, "write_handler was not called with the correct number of bytes written!");
	TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(send_buffer, buffer, bytes_to_send), "Buffer was not sent correctly!");
}

static void test_socket_writesome_fails(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	sendmsg_fake.custom_fake = send_fails;

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(stream, &wbh, write_handler, NULL);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "write_handler was called!");
}

static void test_socket_writesome_blocks(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));

	ssize_t (*custom_fakes[])(int, const struct msghdr *, int) =
	    {
	        send_blocks,
	        send_all};
	SET_CUSTOM_FAKE_SEQ(sendmsg, custom_fakes, ARRAY_SIZE(custom_fakes));

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(stream, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "write_handler was called!");
	s.ev.write_callback(s.ev.context);
	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write_handler was not called exactly once!");
	TEST_ASSERT_EQUAL_MESSAGE(stream, write_handler_fake.arg0_val, "write_handler was not called with correct stream!");
	TEST_ASSERT_EQUAL_MESSAGE(NULL, write_handler_fake.arg1_val, "write_handler was not called with correct handler_context!");
	TEST_ASSERT_EQUAL_MESSAGE(&wbh, write_handler_fake.arg2_val, "write_handler was not called with original buffer!");
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, write_handler_fake.arg3_val, "write_handler was not called with CIO_SUCCESS!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.arg4_val, "write_handler was not called with 0 bytes written!");
}

static void test_socket_writesome_blocks_fails(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));

	ssize_t (*custom_fakes[])(int, const struct msghdr *, int) =
	    {
	        send_blocks,
	        send_all};
	SET_CUSTOM_FAKE_SEQ(sendmsg, custom_fakes, ARRAY_SIZE(custom_fakes));
	cio_linux_eventloop_register_write_fake.return_val = CIO_INVALID_ARGUMENT;

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(stream, &wbh, write_handler, NULL);
	TEST_ASSERT_NOT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value not correct!");

	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "write_handler was called!");
}

static void test_socket_writesome_no_stream(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	sendmsg_fake.custom_fake = send_all;

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(NULL, &wbh, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "write_handler was not called exactly once!");
}

static void test_socket_writesome_no_buffer(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	sendmsg_fake.custom_fake = send_all;

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(stream, NULL, write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "write_handler was not called exactly once!");
}

static void test_socket_writesome_no_handler(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	sendmsg_fake.custom_fake = send_all;

	struct cio_socket s;
	struct cio_write_buffer wbh;
	struct cio_write_buffer wb;

	cio_write_buffer_head_init(&wbh);
	cio_write_buffer_element_init(&wb, buffer, sizeof(buffer));
	cio_write_buffer_queue_tail(&wbh, &wb);

	enum cio_error err = cio_socket_init(&s, &loop, on_close);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_SUCCESS, err, "Return value of cio_socket_init not correct!");
	struct cio_io_stream *stream = s.get_io_stream(&s);

	err = stream->write_some(stream, &wbh, NULL, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(CIO_INVALID_ARGUMENT, err, "Return value not correct!");
	TEST_ASSERT_EQUAL_MESSAGE(0, write_handler_fake.call_count, "write_handler was not called exactly once!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_socket_init);
	RUN_TEST(test_socket_init_socket_create_no_socket);
	RUN_TEST(test_socket_init_socket_create_no_loop);
	RUN_TEST(test_socket_init_socket_create_fails);
	RUN_TEST(test_socket_init_eventloop_add_fails);
	RUN_TEST(test_socket_close_without_hook);
	RUN_TEST(test_socket_close_with_hook);
	RUN_TEST(test_socket_close_no_stream);
	RUN_TEST(test_socket_enable_nodelay);
	RUN_TEST(test_socket_disable_nodelay);
	RUN_TEST(test_socket_nodelay_setsockopt_fails);
	RUN_TEST(test_socket_enable_keepalive);
	RUN_TEST(test_socket_disable_keepalive);
	RUN_TEST(test_socket_disable_keepalive_setsockopt_fails);
	RUN_TEST(test_socket_enable_keepalive_keep_idle_fails);
	RUN_TEST(test_socket_enable_keepalive_keep_intvl_fails);
	RUN_TEST(test_socket_enable_keepalive_keep_cnt);
	RUN_TEST(test_socket_stream_close);
	RUN_TEST(test_socket_readsome);
	RUN_TEST(test_socket_readsome_register_read_fails);
	RUN_TEST(test_socket_readsome_read_blocks);
	RUN_TEST(test_socket_readsome_read_fails);
	RUN_TEST(test_socket_readsome_no_stream);
	RUN_TEST(test_socket_readsome_no_buffer);
	RUN_TEST(test_socket_readsome_no_handler);
	RUN_TEST(test_socket_writesome_all);
	RUN_TEST(test_socket_writesome_parts);
	RUN_TEST(test_socket_writesome_fails);
	RUN_TEST(test_socket_writesome_blocks);
	RUN_TEST(test_socket_writesome_blocks_fails);
	RUN_TEST(test_socket_writesome_no_stream);
	RUN_TEST(test_socket_writesome_no_buffer);
	RUN_TEST(test_socket_writesome_no_handler);
	return UNITY_END();
}

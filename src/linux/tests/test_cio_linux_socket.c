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

DEFINE_FFF_GLOBALS

FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_add, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_read, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VALUE_FUNC(enum cio_error, cio_linux_eventloop_register_write, const struct cio_eventloop *, struct cio_event_notifier *)
FAKE_VOID_FUNC(cio_linux_eventloop_remove, struct cio_eventloop *, const struct cio_event_notifier *)

FAKE_VALUE_FUNC(int, close, int)
FAKE_VALUE_FUNC(ssize_t, read, int, void *, size_t)
FAKE_VALUE_FUNC(ssize_t, send, int, const void *, size_t, int)
FAKE_VALUE_FUNC(int, setsockopt, int, int, int, const void *, socklen_t)

FAKE_VALUE_FUNC0(int, cio_linux_socket_create)

void on_close(struct cio_socket *s);
FAKE_VOID_FUNC(on_close, struct cio_socket *)

void read_handler(struct cio_io_stream *context, void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);
FAKE_VOID_FUNC(read_handler, struct cio_io_stream *, void *, enum cio_error, uint8_t *, size_t)

void write_handler(struct cio_io_stream *context, void *handler_context, enum cio_error err, size_t bytes_transferred);
FAKE_VOID_FUNC(write_handler, struct cio_io_stream *, void *, enum cio_error, size_t)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static uint8_t read_buffer[100];
static size_t available_read_data;
static uint8_t readback_buffer[200];
static size_t bytes_to_send;
static uint8_t send_buffer[200];

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

static ssize_t send_all(int fd, const void *buf, size_t len, int flags)
{
	(void)fd;
	(void)buf;
	(void)flags;
	memcpy(send_buffer, buf, len);
	return len;
}

static ssize_t send_parts(int fd, const void *buf, size_t len, int flags)
{
	(void)fd;
	(void)buf;
	(void)flags;
	(void)len;
	memcpy(send_buffer, buf, bytes_to_send);
	return bytes_to_send;
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
	RESET_FAKE(send);
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

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);
}

static void test_socket_init_socket_create_fails(void)
{
	struct cio_socket s;
	cio_linux_socket_create_fake.custom_fake = socket_create_fails;

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT(err != cio_success);
}

static void test_socket_init_eventloop_add_fails(void)
{
	struct cio_socket s;
	cio_linux_eventloop_add_fake.return_val = cio_invalid_argument;

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT(err != cio_success);
}

static void test_socket_close_without_hook(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, NULL);
	TEST_ASSERT_EQUAL(cio_success, err);

	s.close(&s);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, close_fake.arg0_val);
}

static void test_socket_close_with_hook(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	s.close(&s);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, close_fake.arg0_val);
	TEST_ASSERT_EQUAL(1, on_close_fake.call_count);
	TEST_ASSERT_EQUAL(&s, on_close_fake.arg0_val);
}

static void test_socket_enable_nodelay(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	err = s.set_tcp_no_delay(&s, true);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, setsockopt_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, setsockopt_fake.arg0_val);
	TEST_ASSERT_EQUAL(IPPROTO_TCP, setsockopt_fake.arg1_val);
	TEST_ASSERT_EQUAL(TCP_NODELAY, setsockopt_fake.arg2_val);
}

static void test_socket_disable_nodelay(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	err = s.set_tcp_no_delay(&s, false);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, setsockopt_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, setsockopt_fake.arg0_val);
	TEST_ASSERT_EQUAL(IPPROTO_TCP, setsockopt_fake.arg1_val);
	TEST_ASSERT_EQUAL(TCP_NODELAY, setsockopt_fake.arg2_val);
}

static void test_socket_nodelay_setsockopt_fails(void)
{
	struct cio_socket s;

	setsockopt_fake.custom_fake = setsockopt_fails;

	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	err = s.set_tcp_no_delay(&s, false);
	TEST_ASSERT(cio_success != err);
	TEST_ASSERT_EQUAL(1, setsockopt_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, setsockopt_fake.arg0_val);
	TEST_ASSERT_EQUAL(IPPROTO_TCP, setsockopt_fake.arg1_val);
	TEST_ASSERT_EQUAL(TCP_NODELAY, setsockopt_fake.arg2_val);
}

static void test_socket_enable_keepalive(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(4, setsockopt_fake.call_count);
}

static void test_socket_disable_keepalive(void)
{
	struct cio_socket s;

	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	err = s.set_keep_alive(&s, false, 10, 9, 8);
	TEST_ASSERT_EQUAL(cio_success, err);
	TEST_ASSERT_EQUAL(1, setsockopt_fake.call_count);
}

static void test_socket_disable_keepalive_setsockopt_fails(void)
{
	struct cio_socket s;
	int (*custom_fakes[])(int, int, int, const void *, socklen_t) =
	    {
	        setsockopt_fails,
	    };
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, false, 10, 9, 8);
	TEST_ASSERT(cio_success != err);
	TEST_ASSERT_EQUAL(1, setsockopt_fake.call_count);
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
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT(cio_success != err);
	TEST_ASSERT_EQUAL(1, setsockopt_fake.call_count);
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
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT(cio_success != err);
	TEST_ASSERT_EQUAL(2, setsockopt_fake.call_count);
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
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);

	SET_CUSTOM_FAKE_SEQ(setsockopt, custom_fakes, ARRAY_SIZE(custom_fakes));

	err = s.set_keep_alive(&s, true, 10, 9, 8);
	TEST_ASSERT(cio_success != err);
	TEST_ASSERT_EQUAL(3, setsockopt_fake.call_count);
}

static void test_socket_stream_close(void)
{
	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	struct cio_io_stream *stream = s.get_io_stream(&s);
	stream->close(stream);
	TEST_ASSERT_EQUAL(1, close_fake.call_count);
	TEST_ASSERT_EQUAL(s.ev.fd, close_fake.arg0_val);
}

static void test_socket_readsome(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_ok;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	struct cio_io_stream *stream = s.get_io_stream(&s);

	stream->read_some(stream, readback_buffer, sizeof(readback_buffer), read_handler, NULL);
	TEST_ASSERT_EQUAL(1, read_handler_fake.call_count);
	TEST_ASSERT_EQUAL(stream, read_handler_fake.arg0_val);
	TEST_ASSERT_EQUAL(NULL, read_handler_fake.arg1_val);
	TEST_ASSERT_EQUAL(cio_success, read_handler_fake.arg2_val);
	TEST_ASSERT_EQUAL(readback_buffer, read_handler_fake.arg3_val);
	TEST_ASSERT_EQUAL(data_to_read, read_handler_fake.arg4_val);
	TEST_ASSERT_EQUAL(0, memcmp(read_buffer, readback_buffer, data_to_read));
}

static void test_socket_readsome_register_read_fails(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_ok;
	cio_linux_eventloop_register_read_fake.return_val = cio_invalid_argument;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	struct cio_io_stream *stream = s.get_io_stream(&s);

	stream->read_some(stream, readback_buffer, sizeof(readback_buffer), read_handler, NULL);
	TEST_ASSERT_EQUAL(1, read_handler_fake.call_count);
	TEST_ASSERT_EQUAL(stream, read_handler_fake.arg0_val);
	TEST_ASSERT_EQUAL(NULL, read_handler_fake.arg1_val);
	TEST_ASSERT_NOT_EQUAL(cio_success, read_handler_fake.arg2_val);
	TEST_ASSERT_EQUAL(readback_buffer, read_handler_fake.arg3_val);
	TEST_ASSERT_EQUAL(0, read_handler_fake.arg4_val);
}

static void test_socket_readsome_read_blocks(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_blocks;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	struct cio_io_stream *stream = s.get_io_stream(&s);

	stream->read_some(stream, readback_buffer, sizeof(readback_buffer), read_handler, NULL);
	TEST_ASSERT_EQUAL(0, read_handler_fake.call_count);
}

static void test_socket_readsome_read_fails(void)
{
	static const size_t data_to_read = 12;
	available_read_data = data_to_read;
	memset(read_buffer, 0x12, data_to_read);
	read_fake.custom_fake = read_fails;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	struct cio_io_stream *stream = s.get_io_stream(&s);

	stream->read_some(stream, readback_buffer, sizeof(readback_buffer), read_handler, NULL);
	TEST_ASSERT_EQUAL(1, read_handler_fake.call_count);
	TEST_ASSERT_EQUAL(stream, read_handler_fake.arg0_val);
	TEST_ASSERT_EQUAL(NULL, read_handler_fake.arg1_val);
	TEST_ASSERT_NOT_EQUAL(cio_success, read_handler_fake.arg2_val);
	TEST_ASSERT_EQUAL(readback_buffer, read_handler_fake.arg3_val);
	TEST_ASSERT_EQUAL(0, read_handler_fake.arg4_val);
}

static void test_socket_writesome_all(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	send_fake.custom_fake = send_all;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	struct cio_io_stream *stream = s.get_io_stream(&s);

	stream->write_some(stream, buffer, sizeof(buffer), write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write_handler was not called exactly once!");
	TEST_ASSERT_EQUAL_MESSAGE(stream, write_handler_fake.arg0_val, "write_handler was not called with correct stream!");
	TEST_ASSERT_EQUAL_MESSAGE(NULL, write_handler_fake.arg1_val, "write_handler was not called with correct handler_context!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, write_handler_fake.arg2_val, "write_handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(sizeof(buffer), write_handler_fake.arg3_val, "write_handler was not called with the correct number of bytes written!");
	TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(send_buffer, buffer, sizeof(buffer)), "Buffer was not sent correctly!");
}

static void test_socket_writesome_parts(void)
{
	uint8_t buffer[13];
	memset(buffer, 0x12, sizeof(buffer));
	bytes_to_send = 9;
	send_fake.custom_fake = send_parts;

	struct cio_socket s;
	enum cio_error err = cio_socket_init(&s, NULL, on_close);
	TEST_ASSERT_EQUAL(cio_success, err);
	struct cio_io_stream *stream = s.get_io_stream(&s);

	stream->write_some(stream, buffer, sizeof(buffer), write_handler, NULL);
	TEST_ASSERT_EQUAL_MESSAGE(1, write_handler_fake.call_count, "write_handler was not called exactly once!");
	TEST_ASSERT_EQUAL_MESSAGE(stream, write_handler_fake.arg0_val, "write_handler was not called with correct stream!");
	TEST_ASSERT_EQUAL_MESSAGE(NULL, write_handler_fake.arg1_val, "write_handler was not called with correct handler_context!");
	TEST_ASSERT_EQUAL_MESSAGE(cio_success, write_handler_fake.arg2_val, "write_handler was not called with cio_success!");
	TEST_ASSERT_EQUAL_MESSAGE(bytes_to_send, write_handler_fake.arg3_val, "write_handler was not called with the correct number of bytes written!");
	TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(send_buffer, buffer, bytes_to_send), "Buffer was not sent correctly!");
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_socket_init);
	RUN_TEST(test_socket_init_socket_create_fails);
	RUN_TEST(test_socket_init_eventloop_add_fails);
	RUN_TEST(test_socket_close_without_hook);
	RUN_TEST(test_socket_close_with_hook);
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
	RUN_TEST(test_socket_writesome_all);
	RUN_TEST(test_socket_writesome_parts);
	return UNITY_END();
}

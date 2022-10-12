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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "cio/compiler.h"
#include "cio/error_code.h"
#include "cio/linux_socket_utils.h"
#include "cio/uart.h"
#include "cio/util.h"

static const char DIR_NAME[] = "/dev/serial/by-path/";

static void read_callback(void *context, enum cio_epoll_error error)
{
	struct cio_io_stream *stream = context;
	struct cio_read_buffer *rb = stream->read_buffer;
	struct cio_uart *uart = cio_container_of(stream, struct cio_uart, stream);

	enum cio_error err = CIO_SUCCESS;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		err = cio_linux_get_socket_error(uart->impl.ev.fd);
		stream->read_handler(stream, stream->read_handler_context, err, rb);
		return;
	}

	ssize_t ret = read(uart->impl.ev.fd, rb->add_ptr, cio_read_buffer_space_available(rb));
	if (ret == -1) {
		if (cio_unlikely(errno != EAGAIN)) {
			stream->read_handler(stream, stream->read_handler_context, (enum cio_error)(-errno), rb);
		}
	} else {
		if (ret == 0) {
			err = CIO_EOF;
		} else {
			rb->add_ptr += (size_t)ret;
		}

		stream->read_handler(stream, stream->read_handler_context, err, rb);
	}
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_uart *uart = cio_container_of(stream, struct cio_uart, stream);
	uart->impl.ev.context = stream;
	uart->impl.ev.read_callback = read_callback;
	uart->stream.read_buffer = buffer;
	uart->stream.read_handler = handler;
	uart->stream.read_handler_context = handler_context;

	return CIO_SUCCESS;
}

static void write_callback(void *context, enum cio_epoll_error error)
{
	struct cio_io_stream *stream = context;

	enum cio_error err = CIO_SUCCESS;

	if (cio_unlikely(error != CIO_EPOLL_SUCCESS)) {
		const struct cio_uart *uart = cio_const_container_of(stream, struct cio_uart, stream);
		err = cio_linux_get_socket_error(uart->impl.ev.fd);
	}

	stream->write_handler(stream, stream->write_handler_context, stream->write_buffer, err, 0);
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_uart *uart = cio_container_of(stream, struct cio_uart, stream);
	size_t chain_length = cio_write_buffer_get_num_buffer_elements(buffer);

	const struct cio_write_buffer *wb = buffer->next;
	size_t written = 0;
	bool write_error = false;
	for (size_t i = 0; i < chain_length; i++) {
		ssize_t ret = write(uart->impl.ev.fd, wb->data.element.data, wb->data.element.length);
		if (cio_likely(ret >= 0)) {
			written += (size_t)ret;
			if ((size_t)ret != wb->data.element.length) {
				break;
			}
		} else {
			write_error = true;
			break;
		}

		wb = wb->next;
	}

	if (cio_likely(!write_error)) {
		handler(stream, handler_context, buffer, CIO_SUCCESS, written);
		return CIO_SUCCESS;
	}

	if (cio_likely(errno == EAGAIN)) {
		uart->stream.write_handler = handler;
		uart->stream.write_handler_context = handler_context;
		uart->stream.write_buffer = buffer;
		uart->impl.ev.context = stream;
		uart->impl.ev.write_callback = write_callback;
		return cio_linux_eventloop_register_write(uart->impl.loop, &uart->impl.ev);
	}

	return (enum cio_error)(-errno);
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_uart *port = cio_container_of(stream, struct cio_uart, stream);
	return cio_uart_close(port);
}

static enum cio_error get_current_settings(const struct cio_uart *port, struct termios *tty)
{
	memset(tty, 0, sizeof(*tty));
	int ret = tcgetattr(port->impl.ev.fd, tty);
	if (cio_unlikely(ret == -1)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

size_t cio_uart_get_number_of_uarts(void)
{
	DIR *serial_dir = opendir(DIR_NAME);
	if (serial_dir == NULL) {
		return 0;
	}

	struct dirent *dir_entry = readdir(serial_dir);
	size_t num_uarts = 0;
	while (dir_entry) {
		if (dir_entry->d_type == DT_LNK) {
			num_uarts++;
		}

		dir_entry = readdir(serial_dir);
	}

	closedir(serial_dir);
	return num_uarts;
}

enum cio_error cio_uart_get_ports(struct cio_uart ports[], size_t num_ports_entries, size_t *num_detected_ports)
{
	DIR *serial_dir = opendir(DIR_NAME);
	if (serial_dir == NULL) {
		return CIO_NO_SUCH_FILE_OR_DIRECTORY;
	}

	struct dirent *dir_entry = readdir(serial_dir);
	size_t num_uarts = 0;
	while (dir_entry) {
		if (num_uarts >= num_ports_entries) {
			*num_detected_ports = num_uarts;
			closedir(serial_dir);
			return CIO_SUCCESS;
		}

		if (dir_entry->d_type == DT_LNK) {
			char src_buffer[PATH_MAX + 1];
			strncpy(src_buffer, DIR_NAME, sizeof(src_buffer) - 1);
			size_t len = strlen(src_buffer);
			strncpy(src_buffer + len, dir_entry->d_name, sizeof(src_buffer) - len);

			char dst_buffer[PATH_MAX + 1];
			ssize_t name_len = readlink(src_buffer, dst_buffer, sizeof(dst_buffer) - 1);
			if (cio_unlikely(name_len == -1)) {
				closedir(serial_dir);
				return (enum cio_error)(-errno);
			}

			dst_buffer[name_len] = '\0';

			strncpy(src_buffer, DIR_NAME, sizeof(src_buffer) - 1);
			len = strlen(src_buffer);
			strncpy(src_buffer + len, dir_entry->d_name, sizeof(src_buffer) - len);

			char *rp = realpath(src_buffer, ports[num_uarts].impl.name);
			if (cio_unlikely(rp == NULL)) {
				closedir(serial_dir);
				return (enum cio_error)(-errno);
			}

			num_uarts++;
		}

		dir_entry = readdir(serial_dir);
	}

	*num_detected_ports = num_uarts;
	closedir(serial_dir);
	return CIO_SUCCESS;
}

static enum cio_error set_termios(int fd, struct termios *tty)
{
	int ret = tcflush(fd, TCIFLUSH);
	if (cio_unlikely(ret == -1)) {
		return (enum cio_error)(-errno);
	}
	ret = tcsetattr(fd, TCSANOW, tty);
	if (cio_unlikely(ret == -1)) {
		return (enum cio_error)(-errno);
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_init(struct cio_uart *port, struct cio_eventloop *loop, cio_uart_close_hook_t close_hook)
{
	if (cio_unlikely((port == NULL) || (loop == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	port->impl.ev.fd = open(port->impl.name, (unsigned int)O_RDWR | (unsigned int)O_CLOEXEC | (unsigned int)O_NONBLOCK);
	if (cio_unlikely(port->impl.ev.fd < 0)) {
		return (enum cio_error)(-errno);
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
	tty.c_lflag &= ~(tcflag_t)ICANON; // Disable canonical mode, so receive every byte not so only after newline
	tty.c_lflag &= ~(tcflag_t)ECHO; // Disable echo
	tty.c_lflag &= ~(tcflag_t)ECHOE; // Disable erasure
	tty.c_lflag &= ~(tcflag_t)ECHONL; // Disable new-line echo
	tty.c_lflag &= ~(tcflag_t)ISIG; // Disable interpretation of INTR, QUIT and SUSP
	tty.c_iflag &= ~((tcflag_t)IXON | (tcflag_t)IXOFF | (tcflag_t)IXANY); // Turn off s/w flow ctrl
	tty.c_iflag &= ~((tcflag_t)IGNBRK | (tcflag_t)BRKINT |
	                 (tcflag_t)PARMRK | (tcflag_t)ISTRIP |
	                 (tcflag_t)INLCR | (tcflag_t)IGNCR |
	                 (tcflag_t)ICRNL); // Disable any special handling of received bytes
	tty.c_oflag &= ~(tcflag_t)OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
	tty.c_oflag &= ~(tcflag_t)ONLCR; // Prevent conversion of newline to carriage return/line feed
	tty.c_cc[VTIME] = 0; // No blocking, return immediately with what is available
	tty.c_cc[VMIN] = 0;

	int ret = cfsetspeed(&tty, B115200);
	if (cio_unlikely(ret == -1)) {
		err = (enum cio_error)(-errno);
		goto close_err;
	}

	err = set_termios(port->impl.ev.fd, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto close_err;
	}

	port->close_hook = close_hook;

	port->stream.read_some = stream_read;
	port->stream.write_some = stream_write;
	port->stream.close = stream_close;

	port->impl.loop = loop;

	err = cio_linux_eventloop_add(port->impl.loop, &port->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto close_err;
	}

	err = cio_linux_eventloop_register_read(port->impl.loop, &port->impl.ev);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto ev_reg_read_failed;
	}

	return CIO_SUCCESS;

ev_reg_read_failed:
	cio_linux_eventloop_remove(port->impl.loop, &port->impl.ev);
close_err:
	close(port->impl.ev.fd);
	return err;
}

enum cio_error cio_uart_close(struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	close(port->impl.ev.fd);
	if (port->close_hook != NULL) {
		port->close_hook(port);
	}

	cio_linux_eventloop_unregister_read(port->impl.loop, &port->impl.ev);
	cio_linux_eventloop_remove(port->impl.loop, &port->impl.ev);

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_parity(const struct cio_uart *port, enum cio_uart_parity parity)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	switch (parity) {
	case CIO_UART_PARITY_NONE:
		tty.c_cflag &= ~(tcflag_t)CMSPAR;
		tty.c_cflag &= ~(tcflag_t)PARENB;
		break;
	case CIO_UART_PARITY_ODD:
		tty.c_cflag &= ~(tcflag_t)CMSPAR;
		tty.c_cflag |= (tcflag_t)PARENB;
		tty.c_cflag |= (tcflag_t)PARODD;
		break;
	case CIO_UART_PARITY_EVEN:
		tty.c_cflag &= ~(tcflag_t)CMSPAR;
		tty.c_cflag |= (tcflag_t)PARENB;
		tty.c_cflag &= ~(tcflag_t)PARODD;
		break;
	case CIO_UART_PARITY_MARK:
		tty.c_cflag |= (tcflag_t)CMSPAR;
		tty.c_cflag |= (tcflag_t)PARENB;
		tty.c_cflag |= (tcflag_t)PARODD;
		break;
	case CIO_UART_PARITY_SPACE:
		tty.c_cflag |= (tcflag_t)CMSPAR;
		tty.c_cflag |= (tcflag_t)PARENB;
		tty.c_cflag &= ~(tcflag_t)PARODD;
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	err = set_termios(port->impl.ev.fd, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_get_parity(const struct cio_uart *port, enum cio_uart_parity *parity)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	if ((tty.c_cflag & (tcflag_t)PARENB) == 0) {
		*parity = CIO_UART_PARITY_NONE;
	} else if ((tty.c_cflag & (tcflag_t)CMSPAR) == 0) {
		if ((tty.c_cflag & (tcflag_t)(PARODD)) == (tcflag_t)PARODD) {
			*parity = CIO_UART_PARITY_ODD;
		} else {
			*parity = CIO_UART_PARITY_EVEN;
		}
	} else {
		if ((tty.c_cflag & (tcflag_t)(PARODD)) == (tcflag_t)PARODD) {
			*parity = CIO_UART_PARITY_MARK;
		} else {
			*parity = CIO_UART_PARITY_SPACE;
		}
	}
	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_num_stop_bits(const struct cio_uart *port, enum cio_uart_num_stop_bits num_stop_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	switch (num_stop_bits) {
	case CIO_UART_ONE_STOP_BIT:
		tty.c_cflag &= ~(tcflag_t)CSTOPB;
		break;

	case CIO_UART_TWO_STOP_BITS:
		tty.c_cflag |= (tcflag_t)CSTOPB;
		break;

	default:
		return CIO_INVALID_ARGUMENT;
	}

	err = set_termios(port->impl.ev.fd, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_get_num_stop_bits(const struct cio_uart *port, enum cio_uart_num_stop_bits *num_stop_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	if ((tty.c_cflag & (tcflag_t)CSTOPB) == (tcflag_t)CSTOPB) {
		*num_stop_bits = CIO_UART_TWO_STOP_BITS;
	} else {
		*num_stop_bits = CIO_UART_ONE_STOP_BIT;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_num_data_bits(const struct cio_uart *port, enum cio_uart_num_data_bits num_data_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	switch (num_data_bits) {
	case CIO_UART_5_DATA_BITS:
		tty.c_cflag |= (tcflag_t)CS5;
		break;
	case CIO_UART_6_DATA_BITS:
		tty.c_cflag |= (tcflag_t)CS6;
		break;
	case CIO_UART_7_DATA_BITS:
		tty.c_cflag |= (tcflag_t)CS7;
		break;
	case CIO_UART_8_DATA_BITS:
		tty.c_cflag |= (tcflag_t)CS8;
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	err = set_termios(port->impl.ev.fd, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_get_num_data_bits(const struct cio_uart *port, enum cio_uart_num_data_bits *num_data_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	switch (tty.c_cflag & (tcflag_t)CSIZE) {
	case CS5:
		*num_data_bits = CIO_UART_5_DATA_BITS;
		break;
	case CS6:
		*num_data_bits = CIO_UART_6_DATA_BITS;
		break;
	case CS7:
		*num_data_bits = CIO_UART_7_DATA_BITS;
		break;
	case CS8:
	default:
		*num_data_bits = CIO_UART_8_DATA_BITS;
		break;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_flow_control(const struct cio_uart *port, enum cio_uart_flow_control flow_control)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	switch (flow_control) {
	case CIO_UART_FLOW_CONTROL_NONE:
		tty.c_cflag &= ~(tcflag_t)CRTSCTS;
		tty.c_iflag &= ~((tcflag_t)IXON | (tcflag_t)IXOFF | (tcflag_t)IXANY);
		break;
	case CIO_UART_FLOW_CONTROL_RTS_CTS:
		tty.c_iflag &= ~((tcflag_t)IXON | (tcflag_t)IXOFF | (tcflag_t)IXANY);
		tty.c_cflag |= (tcflag_t)CRTSCTS;
		break;
	case CIO_UART_FLOW_CONTROL_XON_XOFF:
		tty.c_cflag &= ~(tcflag_t)CRTSCTS;
		tty.c_iflag |= ((tcflag_t)IXON | (tcflag_t)IXOFF | (tcflag_t)IXANY);
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	err = set_termios(port->impl.ev.fd, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}

static bool xon_xoff_enabled(tcflag_t iflags)
{
	if (((iflags & (tcflag_t)IXON) == (tcflag_t)IXON) &&
	    ((iflags & (tcflag_t)IXOFF) == (tcflag_t)IXOFF) &&
	    ((iflags & (tcflag_t)IXANY) == (tcflag_t)IXANY)) {
		return true;
	}

	return false;
}

static bool cts_rts_enabled(tcflag_t cflags)
{
	if ((cflags & (tcflag_t)CRTSCTS) == (tcflag_t)CRTSCTS) {
		return true;
	}

	return false;
}

enum cio_error cio_uart_get_flow_control(const struct cio_uart *port, enum cio_uart_flow_control *flow_control)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	if (!cts_rts_enabled(tty.c_cflag) && !xon_xoff_enabled(tty.c_iflag)) {
		*flow_control = CIO_UART_FLOW_CONTROL_NONE;
	} else if (cts_rts_enabled(tty.c_cflag) && !xon_xoff_enabled(tty.c_iflag)) {
		*flow_control = CIO_UART_FLOW_CONTROL_RTS_CTS;
	} else if (!cts_rts_enabled(tty.c_cflag) && xon_xoff_enabled(tty.c_iflag)) {
		*flow_control = CIO_UART_FLOW_CONTROL_XON_XOFF;
	} else {
		return CIO_PROTOCOL_NOT_SUPPORTED;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_baud_rate(const struct cio_uart *port, enum cio_uart_baud_rate baud_rate)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;
	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	int ret = 0;
	switch (baud_rate) {
	case CIO_UART_BAUD_RATE_50:
		ret = cfsetspeed(&tty, (speed_t)B50);
		break;
	case CIO_UART_BAUD_RATE_75:
		ret = cfsetspeed(&tty, (speed_t)B75);
		break;
	case CIO_UART_BAUD_RATE_110:
		ret = cfsetspeed(&tty, (speed_t)B110);
		break;
	case CIO_UART_BAUD_RATE_134:
		ret = cfsetspeed(&tty, (speed_t)B134);
		break;
	case CIO_UART_BAUD_RATE_150:
		ret = cfsetspeed(&tty, (speed_t)B150);
		break;
	case CIO_UART_BAUD_RATE_200:
		ret = cfsetspeed(&tty, (speed_t)B200);
		break;
	case CIO_UART_BAUD_RATE_300:
		ret = cfsetspeed(&tty, (speed_t)B300);
		break;
	case CIO_UART_BAUD_RATE_600:
		ret = cfsetspeed(&tty, (speed_t)B600);
		break;
	case CIO_UART_BAUD_RATE_1200:
		ret = cfsetspeed(&tty, (speed_t)B1200);
		break;
	case CIO_UART_BAUD_RATE_1800:
		ret = cfsetspeed(&tty, (speed_t)B1800);
		break;
	case CIO_UART_BAUD_RATE_2400:
		ret = cfsetspeed(&tty, (speed_t)B2400);
		break;
	case CIO_UART_BAUD_RATE_4800:
		ret = cfsetspeed(&tty, (speed_t)B4800);
		break;
	case CIO_UART_BAUD_RATE_9600:
		ret = cfsetspeed(&tty, (speed_t)B9600);
		break;
	case CIO_UART_BAUD_RATE_19200:
		ret = cfsetspeed(&tty, (speed_t)B19200);
		break;
	case CIO_UART_BAUD_RATE_38400:
		ret = cfsetspeed(&tty, (speed_t)B38400);
		break;
	case CIO_UART_BAUD_RATE_57600:
		ret = cfsetspeed(&tty, (speed_t)B57600);
		break;
	case CIO_UART_BAUD_RATE_115200:
		ret = cfsetspeed(&tty, (speed_t)B115200);
		break;
	case CIO_UART_BAUD_RATE_230400:
		ret = cfsetspeed(&tty, (speed_t)B230400);
		break;
	case CIO_UART_BAUD_RATE_460800:
		ret = cfsetspeed(&tty, (speed_t)B460800);
		break;
	case CIO_UART_BAUD_RATE_500000:
		ret = cfsetspeed(&tty, (speed_t)B500000);
		break;
	case CIO_UART_BAUD_RATE_576000:
		ret = cfsetspeed(&tty, (speed_t)B576000);
		break;
	case CIO_UART_BAUD_RATE_921600:
		ret = cfsetspeed(&tty, (speed_t)B921600);
		break;
	case CIO_UART_BAUD_RATE_1000000:
		ret = cfsetspeed(&tty, (speed_t)B1000000);
		break;
	case CIO_UART_BAUD_RATE_1152000:
		ret = cfsetspeed(&tty, (speed_t)B1152000);
		break;
	case CIO_UART_BAUD_RATE_1500000:
		ret = cfsetspeed(&tty, (speed_t)B1500000);
		break;
	case CIO_UART_BAUD_RATE_2000000:
		ret = cfsetspeed(&tty, (speed_t)B2000000);
		break;
	case CIO_UART_BAUD_RATE_2500000:
		ret = cfsetspeed(&tty, (speed_t)B2500000);
		break;
	case CIO_UART_BAUD_RATE_3000000:
		ret = cfsetspeed(&tty, (speed_t)B3000000);
		break;
	case CIO_UART_BAUD_RATE_3500000:
		ret = cfsetspeed(&tty, (speed_t)B3500000);
		break;
	case CIO_UART_BAUD_RATE_4000000:
		ret = cfsetspeed(&tty, (speed_t)B4000000);
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	if (cio_unlikely(ret == -1)) {
		return (enum cio_error)(-errno);
	}

	err = set_termios(port->impl.ev.fd, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_get_baud_rate(const struct cio_uart *port, enum cio_uart_baud_rate *baud_rate)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	struct termios tty;

	enum cio_error err = get_current_settings(port, &tty);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		return err;
	}

	speed_t speed = cfgetispeed(&tty);
	switch (speed) {
	case B50:
		*baud_rate = CIO_UART_BAUD_RATE_50;
		break;
	case B75:
		*baud_rate = CIO_UART_BAUD_RATE_75;
		break;
	case B110:
		*baud_rate = CIO_UART_BAUD_RATE_110;
		break;
	case B134:
		*baud_rate = CIO_UART_BAUD_RATE_134;
		break;
	case B150:
		*baud_rate = CIO_UART_BAUD_RATE_150;
		break;
	case B200:
		*baud_rate = CIO_UART_BAUD_RATE_200;
		break;
	case B300:
		*baud_rate = CIO_UART_BAUD_RATE_300;
		break;
	case B600:
		*baud_rate = CIO_UART_BAUD_RATE_600;
		break;
	case B1200:
		*baud_rate = CIO_UART_BAUD_RATE_1200;
		break;
	case B1800:
		*baud_rate = CIO_UART_BAUD_RATE_1800;
		break;
	case B2400:
		*baud_rate = CIO_UART_BAUD_RATE_2400;
		break;
	case B4800:
		*baud_rate = CIO_UART_BAUD_RATE_4800;
		break;
	case B9600:
		*baud_rate = CIO_UART_BAUD_RATE_9600;
		break;
	case B19200:
		*baud_rate = CIO_UART_BAUD_RATE_19200;
		break;
	case B38400:
		*baud_rate = CIO_UART_BAUD_RATE_38400;
		break;
	case B57600:
		*baud_rate = CIO_UART_BAUD_RATE_57600;
		break;
	case B115200:
		*baud_rate = CIO_UART_BAUD_RATE_115200;
		break;
	case B230400:
		*baud_rate = CIO_UART_BAUD_RATE_230400;
		break;
	case B460800:
		*baud_rate = CIO_UART_BAUD_RATE_460800;
		break;
	case B500000:
		*baud_rate = CIO_UART_BAUD_RATE_500000;
		break;
	case B576000:
		*baud_rate = CIO_UART_BAUD_RATE_576000;
		break;
	case B921600:
		*baud_rate = CIO_UART_BAUD_RATE_921600;
		break;
	case B1000000:
		*baud_rate = CIO_UART_BAUD_RATE_1000000;
		break;
	case B1152000:
		*baud_rate = CIO_UART_BAUD_RATE_1152000;
		break;
	case B1500000:
		*baud_rate = CIO_UART_BAUD_RATE_1500000;
		break;
	case B2000000:
		*baud_rate = CIO_UART_BAUD_RATE_2000000;
		break;
	case B2500000:
		*baud_rate = CIO_UART_BAUD_RATE_2500000;
		break;
	case B3000000:
		*baud_rate = CIO_UART_BAUD_RATE_3000000;
		break;
	case B3500000:
		*baud_rate = CIO_UART_BAUD_RATE_3500000;
		break;
	case B4000000:
		*baud_rate = CIO_UART_BAUD_RATE_4000000;
		break;
	default:
		return CIO_PROTOCOL_NOT_SUPPORTED;
	}

	return CIO_SUCCESS;
}

struct cio_io_stream *cio_uart_get_io_stream(struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return NULL;
	}

	return &port->stream;
}

const char *cio_uart_get_name(const struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return NULL;
	}

	return port->impl.name;
}

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cio_error_code.h"
#include "cio_eventloop.h"
#include "cio_server_socket.h"
#include "cio_socket.h"

static struct cio_eventloop loop;

static void sighandler(int signum)
{
	(void)signum;
	cio_eventloop_cancel(&loop);
}

static void handle_read(void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred);

static void handle_write(void *handler_context, enum cio_error err, size_t bytes_transferred)
{
	(void)bytes_transferred;
	if (err != cio_success) {
		fprintf(stderr, "write error!\n");
		return;
	}

	struct cio_io_stream *stream = handler_context;
	uint8_t buffer[100];
	stream->read_some(stream, buffer, sizeof(buffer), handle_read, stream);
}

static void handle_read(void *handler_context, enum cio_error err, uint8_t *buf, size_t bytes_transferred)
{
	struct cio_io_stream *stream = handler_context;
	if (err != cio_success) {
		fprintf(stderr, "read error!\n");
		return;
	}

	stream->write_some(stream, buf, bytes_transferred, handle_write, stream);
}

static void handle_accept(struct cio_server_socket *ss, void *handler_context, enum cio_error err, struct cio_socket *socket)
{
	(void)ss;
	(void)handler_context;
	if (err != cio_success) {
		fprintf(stderr, "accept error!\n");
		return;
	}

	struct cio_io_stream *stream = socket->get_io_stream(socket);
	uint8_t buffer[100];
	stream->read_some(socket, buffer, sizeof(buffer), handle_read, stream);
}

int main()
{
	if (signal(SIGTERM, sighandler) == SIG_ERR) {
		return -1;
	}

	if (signal(SIGINT, sighandler) == SIG_ERR) {
		signal(SIGTERM, SIG_DFL);
		return -1;
	}

	enum cio_error err = cio_eventloop_init(&loop);
	if (err != cio_success) {
		return EXIT_FAILURE;
	}

	struct cio_server_socket ss;
	cio_server_socket_init(&ss, &loop, NULL);
	ss.init(&ss, 5);
	ss.set_reuse_address(&ss, true);
	ss.bind(&ss, NULL, 12345);
	ss.accept(&ss, handle_accept, NULL);

	err = cio_eventloop_run(&loop);
	cio_eventloop_destroy(&loop);
	return EXIT_SUCCESS;
}

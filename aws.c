#include "aws.h"

static struct server_t server;
static char request_path[BUFSIZ];
static char http_version[HTTP_VERSION_LEN];

/*
 * Initialize connection structure on given socket.
 */
static struct connection *connection_create(int sockfd)
{
	struct connection *conn = malloc(sizeof(*conn));

	DIE(conn == NULL, "malloc");

	conn->sockfd = sockfd;

	/* Make the socket non blocking */
	int flags = fcntl(conn->sockfd, F_GETFL, 0);

	fcntl(conn->sockfd, F_SETFL, flags | O_NONBLOCK);

	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);

	conn->recv_len = 0;
	conn->send_len = 0;
	conn->sent_bytes = 0;
	conn->headers_were_sent = 0;
	conn->sent_file.fd = -1;

	/* Reinitialize server flags */
	server.can_send = 0;

	return conn;
}

/*
 * Remove connection handler.
 */

static void connection_remove(struct connection *conn)
{
	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;
	free(conn);
}

/*
 * Handle a new connection request on the server socket.
 */

static void handle_new_connection(void)
{
	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;

	/* accept new connection */
	sockfd = accept(server.listenfd, (SSA *) &addr, &addrlen);
	DIE(sockfd < 0, "accept");

	dlog(LOG_ERR, "Accepted connection from: %s:%d\n",
		inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

	/* instantiate new connection handler */
	conn = connection_create(sockfd);

	/* add socket to epoll */
	rc = w_epoll_add_ptr_in(server.epollfd, sockfd, conn);
	DIE(rc < 0, "w_epoll_add_in");
}


static int on_path_cb(http_parser *p, const char *buf, size_t len)
{
	assert(p == &server.request_parser);

	/* Clear request path buffer */
	memset(request_path, 0, BUFSIZ);

	memcpy(request_path, buf, len);

	return 0;
}

static int on_headers_complete_cb(http_parser *p)
{
	assert(p == &server.request_parser);
	server.can_send = 1;

	return 0;
}

static http_parser_settings settings_on_path = HTTP_SETTINGS_INIT();

/*
 * Check if the requested path exists.
 * @return the fd of the file if it exists, -1 otherwise
 */
static int check_path(char *request_path)
{
	/* Check to see if the path is valid */
	if (!strcmp(request_path, "/") || !strlen(request_path))
		return -1;

	/* Check if the file exists */
	int fd = -1;
	struct stat file_info;

	if (!stat(request_path + 1, &file_info))
		fd = open(request_path + 1, O_RDONLY);

	return fd;
}

/*
 * Copy the http response into the send buffer.
 * @return the file that should be handled (if it exists)
 */
static struct sent_file_t set_response(struct connection *conn)
{
	/* Clear buffers */
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);

	/* Get http version */
	sprintf(http_version, "HTTP/%hu.%hu", server.request_parser.http_major,
										  server.request_parser.http_minor);

	/* File descriptor or error (< 0) */
	int response = check_path(request_path);

	struct sent_file_t sent_file = SENT_FILE_INIT();

	char initial_line[INITIAL_LINE_LEN];

	if (response < 0) {
		sprintf(initial_line, "%s %s" CRLF CRLF, http_version, NOT_FOUND);
		conn->send_len = strlen(initial_line);
		memcpy(conn->send_buffer, initial_line, strlen(initial_line));
	} else {
		sprintf(initial_line, "%s %s" CRLF, http_version, OK);
		conn->send_len = strlen(initial_line);
		memcpy(conn->send_buffer, initial_line, strlen(initial_line));

		/* Get file type: static/dynamic */
		sent_file.file_type = strstr(request_path, AWS_REL_STATIC_FOLDER) ?
								STATIC_FILE : DYNAMIC_FILE;

		char message[BUFSIZ];

		memset(message, 0, BUFSIZ);

		/* Get file size */
		struct stat file_info;

		fstat(sent_file.fd, &file_info);
		sent_file.size = file_info.st_size;

		/* Put headers */
		sprintf(message, "%sContent-Length: %lu" CRLF
						 "Connection: close" CRLF CRLF, initial_line, sent_file.size);
		conn->send_len = strlen(message);
		memcpy(conn->send_buffer, message, conn->send_len);
	}

	return sent_file;
}

/*
 * Receive message on socket.
 * Store message in recv_buffer in struct connection.
 */

static enum connection_state receive_message(struct connection *conn)
{
	ssize_t bytes_recv;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}

	bytes_recv = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ, 0);

	if (bytes_recv < 0) {		/* error in communication */
		dlog(LOG_ERR, "Error in communication from: %s\n", abuffer);
		goto remove_connection;
	}
	if (bytes_recv == 0) {		/* connection closed */
		dlog(LOG_INFO, "Connection closed from: %s\n", abuffer);
		goto remove_connection;
	}

	conn->recv_len += bytes_recv;

	conn->state = STATE_DATA_RECEIVED;

	return STATE_DATA_RECEIVED;

remove_connection:
	rc = w_epoll_remove_ptr(server.epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	/* remove current connection */
	connection_remove(conn);

	return STATE_CONNECTION_CLOSED;
}

/*
 *	Send file to socket using libaio async I/O
 */
static void send_dynamic(struct connection *conn, struct sent_file_t *sent_file)
{
	int senderfd = sent_file->fd;
	int receiverfd = conn->sockfd;

	/* Set context */
	io_context_t ctx = {0};

	if (io_setup(2, &ctx) < 0) {
		ERR("io_setup");
		return;
	}

	/* Buffer */
	char buffer[BUFSIZ];

	/* Define I/O operations */
	struct iocb iocb_list[2];

	io_prep_pwrite(&iocb_list[0], receiverfd, buffer, 0, 0);
	io_prep_pread(&iocb_list[1], senderfd, buffer, BUFSIZ, 0);


	struct iocb *piocb[2] = {&iocb_list[0], &iocb_list[1]};
	struct io_event events[2];

	int result = 0;

	while (conn->sent_bytes <= sent_file->size) {
		result = io_submit(ctx, IO_OPS, piocb);

		result = io_getevents(ctx, IO_OPS, IO_OPS, events, NULL);

		if (result < IO_OPS) {
			ERR("io_getevents");
			return;
		}

		/* Stop when there is nothing to be read from the file */
		if (events[1].res == 0)
			break;

		io_prep_pwrite(&iocb_list[0], receiverfd, &buffer, events[1].res, 0);
		conn->sent_bytes += events[1].res;

		/* Update offset */
		io_prep_pread(&iocb_list[1], senderfd, &buffer, BUFSIZ, conn->sent_bytes);
	}

	io_destroy(ctx);
}

/*
 * Send message on socket.
 * Message headers are stored in send_buffer in struct connection.
 */

static enum connection_state send_message(struct connection *conn)
{
	ssize_t bytes_sent;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}

	/* Send headers */
	if (!conn->headers_were_sent) {
		bytes_sent = send(conn->sockfd, conn->send_buffer + conn->sent_bytes,
						  conn->send_len - conn->sent_bytes, 0);
		conn->sent_bytes += bytes_sent;

		if (bytes_sent < 0) {		/* error in communication */
			dlog(LOG_ERR, "Error in communication to %s\n", abuffer);
			goto remove_connection;
		}
		if (bytes_sent == 0) {		/* connection closed */
			dlog(LOG_INFO, "Connection closed to %s\n", abuffer);
			goto remove_connection;
		}

		if (conn->sent_bytes == conn->send_len) {
			conn->sent_bytes = 0;
			conn->headers_were_sent = 1;
		}

		return STATE_DATA_IS_BEING_SENT;
	}

	/* Send file if necessary */
	struct sent_file_t sent_file = conn->sent_file;

	switch (sent_file.file_type) {
		case NO_FILE: {
			break;
		}
		case STATIC_FILE: {
			conn->sent_bytes += sendfile(conn->sockfd, sent_file.fd, sent_file.offset,
											sent_file.size - conn->sent_bytes);

			sent_file.offset = (void *)conn->sent_bytes;

			if (conn->sent_bytes < sent_file.size) {
				conn->state = STATE_DATA_IS_BEING_SENT;
				return STATE_DATA_IS_BEING_SENT;
			}
			break;
		}
		case DYNAMIC_FILE: {
			send_dynamic(conn, &sent_file);
			break;
		}
	}

	/* all done - remove out notification */
	rc = w_epoll_update_ptr_in(server.epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_update_ptr_in");

	conn->state = STATE_DATA_SENT;

	close(sent_file.fd);

remove_connection:
	rc = w_epoll_remove_ptr(server.epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	/* remove current connection */
	connection_remove(conn);

	return STATE_CONNECTION_CLOSED;
}

/*
 * Handle a client request on a client connection.
 */

static void handle_client_request(struct connection *conn)
{
	int rc;
	enum connection_state ret_state;

	ret_state = receive_message(conn);
	if (ret_state == STATE_CONNECTION_CLOSED)
		return;

	ssize_t bytes_recv = conn->recv_len;

	/* Set up http parser */
	size_t bytes_parsed;

	http_parser_init(&server.request_parser, HTTP_REQUEST);

	bytes_parsed = http_parser_execute(&server.request_parser,
									   &settings_on_path,
									   conn->recv_buffer, bytes_recv);

	if (!server.can_send || !bytes_parsed)
		return;

	dlog(LOG_DEBUG, "Received message: %s\n", conn->recv_buffer);

	/* Analize request and get file */
	conn->sent_file = set_response(conn);

	/* Make socket output-only */
	rc = w_epoll_update_ptr_out(server.epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_update_ptr_out");

	server.can_send = 0;
}

int main(void)
{
	int rc;

	/* init multiplexing */
	server.epollfd = w_epoll_create();
	DIE(server.epollfd < 0, "w_epoll_create");

	/* create server socket */
	server.listenfd = tcp_create_listener(AWS_LISTEN_PORT,
		DEFAULT_LISTEN_BACKLOG);
	DIE(server.listenfd < 0, "tcp_create_listener");

	rc = w_epoll_add_fd_in(server.epollfd, server.listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");

	dlog(LOG_INFO, "Server waiting for connections on port %d\n",
		AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* wait for events */
		rc = w_epoll_wait_infinite(server.epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");

		/*
		 * switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */

		if (rev.data.fd == server.listenfd) {
			dlog(LOG_DEBUG, "New connection\n");
			if (rev.events & EPOLLIN)
				handle_new_connection();
		} else {
			if (rev.events & EPOLLIN) {
				dlog(LOG_DEBUG, "New message\n");
				handle_client_request(rev.data.ptr);
			}
			if (rev.events & EPOLLOUT) {
				dlog(LOG_DEBUG, "Ready to send message\n");
				send_message(rev.data.ptr);
			}
		}
	}

	return 0;
}


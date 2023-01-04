#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libaio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/sendfile.h>

#include "aws.h"

struct server_t server;
static char request_path[BUFSIZ];
static char http_version[HTTP_VERSION_LEN];

static int on_path_cb(http_parser *p, const char *buf, size_t len)
{
	assert(p == &server.request_parser);
	memcpy(request_path, buf, len);

	return 0;
}

static http_parser_settings settings_on_path = HTTP_SETTINGS_INIT()

static int check_path(char *request_path) {
	/* Check to see if the path is valid */
	if (!strcmp(request_path, "/") || !strlen(request_path)) {
		return -1;
    }

	/* Check if the file exists */
	int fd = -1;
	struct stat file_info;
	if (!stat(request_path + 1, &file_info)) {
		fd = open(request_path + 1, O_RDONLY);
	}

	return fd;
}

/*
 * Copy the http response into the send buffer
 * and return the file that should be handled (if it exists)
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

	struct sent_file_t sent_file = {.file_type = NO_FILE, .fd = response};
    
	if (response < 0) {
		char error[BUFSIZ];
		sprintf(error, "%s %s\r\n\r\n", http_version, NOT_FOUND);
		conn->send_len = strlen(error);
		memcpy(conn->send_buffer, error, strlen(error));
	} else {
		char success[BUFSIZ];
		sprintf(success, "%s %s\r\n\r\n", http_version, OK);
		conn->send_len = strlen(success);
		memcpy(conn->send_buffer, success, strlen(success));

		if (strstr(request_path, AWS_ABS_STATIC_FOLDER)) {
			sent_file.file_type = STATIC_FILE;
		} else {
			sent_file.file_type = DYNAMIC_FILE;
		}
		//sent_file.file_type = STATIC_FILE;
	}

	/* Clear request path */
	memset(request_path, 0, BUFSIZ);

	return sent_file;
}

/*
 * Receive message on socket.
 * Store message in recv_buffer in struct connection.
 */

static enum connection_state receive_message(struct connection *conn)
{
    size_t bytes_parsed;

	ssize_t bytes_recv;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}

	bytes_recv = recv(conn->sockfd, conn->recv_buffer, BUFSIZ, 0);
	if (bytes_recv < 0) {		/* error in communication */
		dlog(LOG_ERR, "Error in communication from: %s\n", abuffer);
		goto remove_connection;
	}
	if (bytes_recv == 0) {		/* connection closed */
		dlog(LOG_INFO, "Connection closed from: %s\n", abuffer);
		goto remove_connection;
	}

	/* Set up http parser */
	http_parser_init(&server.request_parser, HTTP_REQUEST);

    bytes_parsed = http_parser_execute(&server.request_parser, &settings_on_path, conn->recv_buffer, strlen(conn->recv_buffer));

	printf("Parsed simple HTTP request (bytes: %lu), path: %s\n", bytes_parsed, request_path);
	dlog(LOG_DEBUG, "Received message from: %s\n", abuffer);

	printf("Received buffer: %s--\n", conn->recv_buffer);

	conn->recv_len = bytes_recv;
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
 * Send message on socket.
 * Store message in send_buffer in struct connection.
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

	/* Analize request */
	struct sent_file_t sent_file = set_response(conn);

	bytes_sent = send(conn->sockfd, conn->send_buffer, conn->send_len, 0);
	if (bytes_sent < 0) {		/* error in communication */
		dlog(LOG_ERR, "Error in communication to %s\n", abuffer);
		goto remove_connection;
	}
	if (bytes_sent == 0) {		/* connection closed */
		dlog(LOG_INFO, "Connection closed to %s\n", abuffer);
		goto remove_connection;
	}

	dlog(LOG_DEBUG, "Sending message to %s\n", abuffer);

	printf("Message sent: %s\n", conn->send_buffer);

	/* Send file if necessary */
	switch (sent_file.file_type) {
		case NO_FILE: {
			break;
		}
		case STATIC_FILE: {
			printf("Static file\n");
			sendfile(conn->sockfd, sent_file.fd, SEEK_SET, BUFSIZ);
			break;
		}
		case DYNAMIC_FILE: {
			printf("Dynamic file\n");
			break;
		}
	}

	/* all done - remove out notification */
	rc = w_epoll_update_ptr_in(server.epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_update_ptr_in");

	conn->state = STATE_DATA_SENT;

	return STATE_DATA_SENT;

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

	/* add socket to epoll for out events */
	rc = w_epoll_update_ptr_inout(server.epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_add_ptr_inout");
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

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

#include "aws.h"

struct server_t server;
static char request_path[BUFSIZ];

static int on_path_cb(http_parser *p, const char *buf, size_t len)
{
	assert(p == &server.request_parser);
	memcpy(request_path, buf, len);

	return 0;
}

static http_parser_settings settings_on_path = HTTP_SETTINGS_INIT()

static char *get_response(char *request_path) {
	if (!strcmp(request_path, "/") || !strlen(request_path)) {
        char *error = "HTTP 404 Not Found\n";
		memcpy(request_path, error, strlen(error));
    }

	return request_path;
}

/*
 * Copy the response into the send buffer
 */
static void set_response(struct connection *conn)
{
	/* Clear buffers */
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);

	char *response = get_response(request_path);
	printf("RESPONSE: %s\n", response);
    
	conn->send_len = strlen(response);
	memcpy(conn->send_buffer, response, conn->send_len);

	/* Clear request path */
	memset(request_path, 0, BUFSIZ);
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

	printf("Message sent: %s--\n", conn->send_buffer);

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

	set_response(conn);

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

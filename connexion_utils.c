#include "connexion_utils.h"

/* server */
extern struct server_t server;

/*
 * Initialize connection structure on given socket.
 */
struct connection *connection_create(int sockfd)
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
	conn->received_bytes = 0;
	conn->headers_were_sent = 0;

	return conn;
}

/*
 * Remove connection handler.
 */

void connection_remove(struct connection *conn)
{
	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;
	free(conn);
}

/*
 * Handle a new connection request on the server socket.
 */

void handle_new_connection(void)
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
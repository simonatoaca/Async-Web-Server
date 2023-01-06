#ifndef CONN_UTILS_H_
#define CONN_UTILS_H_

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

#include "util.h"
#include "debug.h"
#include "lin/sock_util.h"
#include "lin/w_epoll.h"
#include "http-parser/http_parser.h"
#include "aws.h"

enum connection_state {
	STATE_DATA_RECEIVED,
    STATE_DATA_IS_BEING_SENT,
    STATE_DATA_IS_BEING_RECEIVED,
	STATE_DATA_SENT,
	STATE_CONNECTION_CLOSED
};

/* structure acting as a connection handler */
struct connection {
	int sockfd;
	/* buffers used for receiving messages and then echoing them back */
	char recv_buffer[BUFSIZ];
	size_t recv_len;
    size_t received_bytes;
	char send_buffer[BUFSIZ];
	size_t send_len;
    size_t sent_bytes;
	enum connection_state state;
	int headers_were_sent;
	struct sent_file_t sent_file;
};

struct connection *connection_create(int sockfd);
void connection_remove(struct connection *conn);
void handle_new_connection(void);

#endif
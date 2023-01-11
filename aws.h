/*
 * Asynchronous Web Server - header file (macros and structures)
 *
 * 2011-2017, Operating Systems
 */

#ifndef AWS_H_
#define AWS_H_		1

#ifdef __cplusplus
extern "C" {
#endif

#define AWS_LISTEN_PORT		8888
#define AWS_DOCUMENT_ROOT	"./"
#define AWS_REL_STATIC_FOLDER	"static/"
#define AWS_REL_DYNAMIC_FOLDER	"dynamic/"
#define AWS_ABS_STATIC_FOLDER	(AWS_DOCUMENT_ROOT AWS_REL_STATIC_FOLDER)
#define AWS_ABS_DYNAMIC_FOLDER	(AWS_DOCUMENT_ROOT AWS_REL_DYNAMIC_FOLDER)

#ifdef __cplusplus
}
#endif


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

#include "util.h"
#include "debug.h"
#include "sock_util.h"
#include "w_epoll.h"
#include "http_parser.h"

#include <libaio.h>

#define HTTP_SETTINGS_INIT() {	\
	.on_message_begin = 0,		\
	.on_path = on_path_cb,		\
	.on_query_string = 0,		\
	.on_url = 0,				\
	.on_fragment = 0,			\
	.on_header_field = 0,		\
	.on_header_value = 0,		\
	.on_headers_complete = on_headers_complete_cb,\
	.on_body = 0,				\
	.on_message_complete = 0	\
}

#define SENT_FILE_INIT() {	\
	.file_type = NO_FILE,			\
	.fd = response,					\
	.size = 0,						\
	.offset = SEEK_SET				\
}

#define HTTP_VERSION_LEN 10
#define INITIAL_LINE_LEN 50

#define NOT_FOUND "404 Not Found"
#define OK "200 OK"

#define CRLF "\r\n"

#define IO_OPS 2

enum connection_state {
	STATE_DATA_RECEIVED,
	STATE_DATA_IS_BEING_SENT,
	STATE_DATA_IS_BEING_RECEIVED,
	STATE_DATA_SENT,
	STATE_CONNECTION_CLOSED
};

enum file_type_t {
	NO_FILE,
	STATIC_FILE,
	DYNAMIC_FILE
};

struct sent_file_t {
	enum file_type_t file_type;
	int fd;
	size_t size;
	off_t *offset;
};

/* structure acting as a connection handler */
struct connection {
	int sockfd;
	/* buffers used for receiving messages and then echoing them back */
	char recv_buffer[BUFSIZ];
	size_t recv_len;

	char send_buffer[BUFSIZ];
	size_t send_len;
	size_t sent_bytes;

	enum connection_state state;
	int headers_were_sent;
	struct sent_file_t sent_file;
};

typedef struct server_t {
	int listenfd;
	int epollfd;
	http_parser request_parser;
	int can_send;
} server_t;

#endif /* AWS_H_ */

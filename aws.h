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

#include "util.h"
#include "debug.h"
#include "lin/sock_util.h"
#include "lin/w_epoll.h"
#include "http-parser/http_parser.h"
#include "connexion_utils.h"

typedef struct server_t {
	struct connection *connexions;
	int listenfd;
	int epollfd;
	http_parser request_parser;
} server;

#define HTTP_SETTINGS_INIT() { \
	/* on_message_begin */ 0, \
	/* on_header_field */ 0, \
	/* on_header_value */ 0, \
	/* on_path */ on_path_cb, \
	/* on_url */ 0, \
	/* on_fragment */ 0, \
 	/* on_query_string */ 0, \
	/* on_body */ 0, \
	/* on_headers_complete */ 0, \
	/* on_message_complete */ 0 \
};

#endif /* AWS_H_ */

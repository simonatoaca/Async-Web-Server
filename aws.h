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

#define HTTP_SETTINGS_INIT() { \
  .on_message_begin = 0, \
  .on_path = on_path_cb, \
  .on_query_string = 0, \
  .on_url = 0, \
  .on_fragment = 0, \
  .on_header_field = 0, \
  .on_header_value = 0, \
  .on_headers_complete = on_headers_complete_cb, \
  .on_body = 0, \
  .on_message_complete = 0 \
};

#define SENT_FILE_INIT() {\
		.file_type = NO_FILE, \
		.fd = response,	\
		.size = 0, \
		.offset = SEEK_SET	\
};

#define HTTP_VERSION_LEN 10
#define INITIAL_LINE_LEN 50

#define NOT_FOUND "404 Not Found"
#define OK "200 OK"

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

typedef struct server_t {
	int listenfd;
	int epollfd;
	http_parser request_parser;
	int can_send;
} server_t;

#endif /* AWS_H_ */

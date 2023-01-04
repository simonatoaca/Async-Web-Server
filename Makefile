CC=gcc
CFLAGS=-Wall -Wextra -g
LDFLAGS=-laio

aws: aws.c lin/sock_util.c http-parser/http_parser.c connexion_utils.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

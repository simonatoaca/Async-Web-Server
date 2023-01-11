CC=gcc
CFLAGS=-Wall -Wextra -g
LDFLAGS=-laio

aws: aws.c sock_util.c http_parser.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm aws

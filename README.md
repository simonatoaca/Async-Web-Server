# Asynchronous Web Server - SO Homework 3
### Copyright 2023 Toaca Alexandra Simona

### Implementation
- I started from the epoll echo server example
- A ```server_t``` struct is used to store the server-specific data
- The server reads from a socket until the header is read completely
(to make sure the request path and other info is received before
sending any message back). This is implemented by using the server flag
```can_send```, which becomes 1 in the ```on_headers_complete``` callback
of the ```http_parser```.
- the ```handle_client_request``` method reads the message from the socket
and **only** when the ```can_send``` flag is 1, it parses the request path
and calls the ```set_response``` method.
- the ```set_response``` method writes the response headers (404 Not Found
if the requested file does not exist, 200 OK, otherwise) and returns
the requested file (if it does not exist, the ```file_type``` field
of the structure is of type ```NO_FILE```).
- If the file exists, the ```file_type``` indicates whether it is **STATIC**
or **DYNAMIC**.
- After the request is handled, the socket is marked as an **out**-only socket
and ```send_message``` is called until everything is sent.
- The ```send_message``` method first sends the headers of the response in their
entirety (the method is called multiple times until this is accomplished).
- After the headers, the file is sent, depending on its type
- The **STATIC** files are sent by using the mechanism of **zero-copy**, 
implemented by the ```sendfile``` method.
- The **DYNAMIC** files are sent by using ```libaio``` async I/O methods, used
in the ```send_dynamic``` method.
- ```send_dynamic``` works by pairing a read from the server file with
a write to the socket. First, a write is done, then a read. Why? The first write
will write 0 bytes, and the read will return how many bytes the next write will
send. By putting that in a loop, the write will send exactly the number of bytes
that were read from the file. When everything is read, the loop terminates.
- After sending the whole file, the connection is closed.

### What I have learned
- Working with sockets and how sometimes they don't receive the whole buffer
sent
- Event-driven programming (EPOLL)
- The basic idea of what goes on when a HTTP request is made
- HTTP requests and their headers
- Working with the libaio library

### Comments
- This has been a fun and interesting homework. It was very
satisfying to send HTTP requests from my terminal and see how the
server logged every action and then sent back the response.
- I was thinking of trying to also implement the POST and HEAD requests
- Also the server could be looking at the file extension and setting
the ```Content-Type``` header according to that

### How to use
- The Makefile builds the executable ```aws```, which is the server,
ready to take requests.




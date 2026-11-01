*This project has been created as part of the 42 curriculum by irfei, ajamoun*

# Webserv

## Description

Webserv is an HTTP/1.1 server written in C++98. It uses poll() to manage
listening sockets, client connections, and CGI pipes. It supports static
files, configurable routes, GET, POST, DELETE, uploads, redirects,
custom error pages, and CGI execution. The project also includes a
simple cookie and session demonstration.

## Instructions

Compile:

    make

Run with the default configuration:

    ./webserv

Run with a specific configuration:

    ./webserv configs/default.conf

Open http://127.0.0.1:8080/ in a browser. Stop the server with Ctrl-C.

Run the integration tests:

    make test

Requirements: a C++ compiler supporting C++98, make, and Python 3.8+
for the test suite. The example CGI configuration uses /usr/bin/python3,
/usr/bin/php-cgi, and /usr/bin/bash. Install the interpreters you want
to demonstrate and update their paths in the configuration if necessary.

The default configuration stores uploaded files in www/uploads.
Body limits and allowed methods are configured per server or location.

## Resources

- HTTP semantics: https://www.rfc-editor.org/rfc/rfc9110
- HTTP/1.1 messaging: https://www.rfc-editor.org/rfc/rfc9112
- CGI: https://www.rfc-editor.org/rfc/rfc3875
- Project subject: subject.pdf
- Project documentation: Docs/

### AI usage

We used AI to help us understand the project's concepts, including HTTP,
non-blocking I/O, poll(), CGI, cookies, and sessions. We also used it to
compare ways to organize the project and choose a structure that fits
its components and responsibilities.


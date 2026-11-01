*This project has been created as part of the 42 curriculum by YOUR_42_LOGIN, ADIL_42_LOGIN.*

# Webserv

The author names above are placeholders: replace them with both exact 42 logins
before submission.

## Description

Webserv is a C++98 HTTP server. A single poll loop handles listening sockets,
client connections and CGI pipes. Configuration chooses the listening address,
port and rules for each URL location.

The Config/Core owner implements configuration, startup, transport, resource
ownership, the request parser adapter and all CGI. Adil owns the HTTP parser,
response handlers, MIME types and bonus cookies/sessions. See the
[current review](Docs/final_core_review.md) for known pending HTTP work.

## Instructions

Build on Linux with a C++ compiler and Make. Python 3 at `/usr/bin/python3` is
the interpreter configured in the CGI example and is also used by the tests.

```sh
make
./webserv
```

Launch from the repository root: example filesystem paths are relative to the
working directory. The default config listens on `127.0.0.1:8080`.

```sh
curl http://127.0.0.1:8080/
curl 'http://127.0.0.1:8080/cgi-bin/?name=example'
curl --data-binary 'hello CGI' http://127.0.0.1:8080/cgi-bin/test.py/extra
curl --data-binary 'uploaded content' http://127.0.0.1:8080/uploads/example.txt
curl http://127.0.0.1:8080/uploads/
curl -X DELETE http://127.0.0.1:8080/uploads/example.txt
curl -i http://127.0.0.1:8080/redirect
curl -i http://127.0.0.1:8080/missing
```

Stop with Ctrl-C before testing the two-site config:

```sh
./webserv configs/test.conf
# Visit http://127.0.0.1:8080/ and http://127.0.0.1:8081/
```

Configured ports must be available. Use `make clean`, `make fclean` and
`make re` to remove objects, remove the executable, or rebuild.

```sh
make test-owned     # Config/Core/CGI and current HTTP integration: expected to pass
make test-http      # Outstanding HTTP/bonus acceptance checks: currently fails
make test          # Both groups; fails while HTTP-owner issues remain
```

The test scripts use the Python standard library. Some cleanup/resource checks
use Linux `/proc`. Unused unfinished session sources are excluded from the
mandatory executable; the bonus is not yet integrated.

## Resources

- [Project subject](subject.pdf): the requirements used for this implementation.
- [CGI/1.1, RFC 3875](https://www.rfc-editor.org/rfc/rfc3875.html): environment,
  input/output and redirects.
- [HTTP/1.1, RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html): message framing
  and connection management. This project implements a documented subset.
- [Linux poll manual](https://man7.org/linux/man-pages/man2/poll.2.html): readiness
  events and descriptor monitoring.
- [Configuration guide](Docs/config.md), [parser contract](Docs/request_parser_contract.md),
  and [final review](Docs/final_core_review.md).

AI assistance was used to explain, implement, review and test configuration,
Core, CGI and the integration adapter, and to draft documentation and test
fixtures. The HTTP, MIME and session files were imported from the teammate's
work and were preserved unchanged during this integration review.

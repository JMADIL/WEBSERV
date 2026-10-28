# 🔵 My Part — Config Parsing & Core

> What I need to build, understand, and test for the 42 webserv project.
> Based on the local subject (version 24.1), `architecture.md`, and my teammate's `my_part.md`.

---

## 📋 Table of Contents

1. [Overview](#overview)
2. [Subject Rules and Allowed Functions](#subject-rules-and-allowed-functions)
3. [Files I Own](#files-i-own)
4. [Component 1: LocationBlock](#component-1-locationblock)
5. [Component 2: ServerBlock](#component-2-serverblock)
6. [Component 3: Config](#component-3-config)
7. [Component 4: Client](#component-4-client)
8. [Component 5: Server](#component-5-server)
9. [Component 6: main.cpp and Makefile](#component-6-maincpp-and-makefile)
10. [What I Need From My Teammate](#what-i-need-from-my-teammate)
11. [Error Handling](#error-handling)
12. [My Task Checklist](#my-task-checklist)
13. [Testing Guide](#testing-guide)
14. [Three-Day Plan](#three-day-plan)
15. [Understanding Checklist](#understanding-checklist)

---

## Overview

I own the configuration and the machinery that keeps the server running. My teammate owns HTTP request parsing, response generation, file handling, and CGI execution.

```text
configuration file
       |
       v
Config -> ServerBlock[] -> LocationBlock[]
       |
       v
Server: create listening sockets -> one poll() loop
       |
       +-> accept connection -> Client
       +-> socket readable -> recv -> teammate's parser
       +-> request complete -> teammate's response builder
       +-> socket writable -> send remaining response bytes
       +-> CGI pipe ready -> teammate's CGI handler
       +-> disconnect / timeout -> cleanup
```

**Important:** `architecture.md` and `my_part.md` use “you” for my friend. In this document, “I” means the person responsible for Config Parsing & Core.

### Current repository status

At the time this guide was written, the Config/Core files and Makefile were absent. Most HTTP/CGI source files and several headers were empty placeholders; `HttpRequest.hpp` and `HttpResponse.hpp` contained initial declarations. The existing documents describe a planned interface, not a working implementation.

All interfaces below are **proposed contracts to agree on together**, not declarations already implemented in the repository. This guide does not implement the server.

---

## Subject Rules and Allowed Functions

Source: `subject.pdf`, printed pages 3, 6, 8–11. Printed page numbers are one less than PDF viewer page numbers.

### Rules that directly affect my code

| Rule | What it means for my part |
|------|--------------------------|
| C++98, no external libraries or Boost | Use standard containers and ordinary classes; no `auto`, `nullptr`, lambdas, `std::thread`, or `std::to_string` |
| Build with `c++ -Wall -Wextra -Werror`; support `-std=c++98` | Put these flags in the Makefile |
| One `poll()` or equivalent for all waiting I/O, including listening | One event loop monitors listeners, clients, and CGI pipes |
| Monitor reading and writing simultaneously | The poll set must serve readers and writers in the same loop; request `POLLOUT` only when that descriptor has pending output |
| No socket/pipe read or write without prior readiness | Dispatch `recv/read` from readable events and `send/write` from writable events |
| Do not use `errno` to adjust behavior after read/write | Do not copy the usual `EAGAIN`/`EINTR` retry loops after `read`, `write`, `recv`, or `send` |
| Regular disk files are exempt from readiness monitoring | Config files and regular content files do not need to be added to poll |
| Requests must never hang indefinitely | Bound idle connections, incomplete requests, pending sends, and CGI execution |
| `fork()` only for CGI | Never fork per client |
| Listen on multiple interface:port pairs | Multiple configured sites/ports are mandatory |
| Virtual hosts are out of scope, but allowed | Host-name routing is optional in this subject version, despite the architecture including it |
| At least one CGI type | CGI execution belongs to my friend; integrating its pipes belongs to me |

The subject's macOS section restricts `fcntl()` flags to `F_SETFL`, `O_NONBLOCK`, and `FD_CLOEXEC`. We are currently on Linux; keep any platform-specific flag choices explicit rather than copying unrestricted macOS examples.

### Complete external-function list from the subject

```text
execve, pipe, strerror, gai_strerror, errno, dup, dup2, fork,
socketpair, htons, htonl, ntohs, ntohl,
select, poll, epoll (epoll_create, epoll_ctl, epoll_wait),
kqueue (kqueue, kevent), socket, accept, listen, send, recv,
shutdown, chdir, bind, connect, getaddrinfo, freeaddrinfo,
setsockopt, getsockname, getprotobyname, fcntl, close, read,
write, waitpid, kill, signal, access, stat, open,
opendir, readdir, closedir.
```

`errno` appears in the list, but the specific restriction after read/write still applies. Being listed does not mean a function must be used.

### Functions I should understand first

| Functions | Purpose |
|-----------|---------|
| `socket` | Create a socket descriptor |
| `setsockopt` | Configure options such as address reuse; does not permit two live servers to claim the same listener arbitrarily |
| `getaddrinfo`, `freeaddrinfo` | Resolve configured address/port into socket addresses and release results |
| `htons`, `htonl`, `ntohs`, `ntohl` | Convert port/address integer byte order when constructing or inspecting addresses manually |
| `bind`, `listen` | Attach socket to interface:port, then make it a listener |
| `fcntl` | Set non-blocking mode; check failure for listeners, accepted clients, and parent CGI pipe ends |
| `poll` | Wait for events on many descriptors together |
| `accept` | Obtain a new connected socket; keep the listening socket open |
| `recv`, `send` | Transfer some bytes, possibly fewer than requested |
| `close`, `shutdown` | Release a descriptor / control socket directions; `shutdown` does not replace `close` |
| `signal` | Handle shutdown requests and prevent `SIGPIPE` from terminating the server |
| `waitpid`, `kill` | Coordinate CGI child completion and termination with my teammate |
| `strerror`, `gai_strerror` | Explain setup/address errors; not a reason to inspect `errno` after socket/pipe transfers |

Configuration parsing can use C++98 `std::ifstream`, `std::string`, `std::vector`, `std::map`, and `std::istringstream`. No parser library is needed.

---

## Files I Own

```text
Makefile                         # shared build agreement
configs/
  default.conf
  test.conf
includes/
  Config.hpp
  ServerBlock.hpp
  LocationBlock.hpp
  Server.hpp
  Client.hpp                     # shared interface; I own core state
srcs/
  main.cpp
  config/
    Config.cpp
    ServerBlock.cpp
    LocationBlock.cpp
  core/
    Server.cpp
    Client.cpp
```

Shared helpers should exist only when both parts need them. My friend owns `HttpRequest`, `HttpResponse`, `RequestParser`, `ResponseBuilder`, `CgiHandler`, and `MimeTypes`.

---

## Component 1: LocationBlock

A location is a route's settings. It stores data; it does not call `recv`, generate HTML, or launch CGI.

### Proposed getters

```cpp
const std::string& getPath() const;
const std::string& getRoot() const;
const std::string& getIndex() const;
const std::vector<std::string>& getAllowedMethods() const;
bool getAutoindex() const;
int getRedirectCode() const;                  // 0 means no redirect
const std::string& getRedirectURL() const;
const std::map<std::string, std::string>& getCgiExtension() const;
const std::string& getUploadPath() const;
size_t getClientMaxBodySize() const;           // effective limit in bytes
```

Provide parser-side setters or construction methods, then expose const getters to the HTTP code. Initialize every field.

### Defaults: project decisions, not subject requirements

Choose and document defaults together. A simple policy is: autoindex off, no redirect, no CGI, no upload permission without `upload_path`, `index.html` as the index, GET as the default method, and a 1 MiB default body limit inherited from the server. Require `root` for file-serving locations; redirect-only locations need no root.

If a location can override the body limit, keep an explicit “was set” flag during parsing. A missing value must not be confused with an explicitly configured zero-byte limit. Resolve inheritance after the complete server block is parsed so directive order does not change behavior.

### Path mapping must match the subject

The subject gives this example:

```text
location /kapouet, configured directory /tmp/www
request /kapouet/pouic/toto/pouet
file    /tmp/www/pouic/toto/pouet
```

Use that mapping consistently with my friend: remove the matched location prefix, then join the remaining path to its configured directory. The directive name `root` in our custom format does not mean we have implemented every NGINX `root` rule.

HTTP code must normalize and validate paths and keep requests inside the configured directory. Config code only stores and validates the configuration.

---

## Component 2: ServerBlock

A server block describes a site: listening address, port, optional name, error pages, body limit, and locations.

### Proposed getters

```cpp
const std::string& getHost() const;
int getPort() const;
const std::string& getServerName() const;
size_t getClientMaxBodySize() const;
const std::vector<LocationBlock>& getLocations() const;
std::string getErrorPage(int statusCode) const; // empty if not configured
const LocationBlock* matchLocation(const std::string& uriPath) const;
```

For the three-day version, one `listen` per server block is a manageable grammar. Use more server blocks for more interface:port pairs. Support at least `listen 8080;` and `listen 127.0.0.1:8081;`; define a host default for the port-only form, for example `0.0.0.0`.

### Location lookup

Use the longest matching route, with a documented boundary policy. A practical policy is that `/uploads` matches `/uploads` and `/uploads/photo.jpg`, but not `/uploads-old`; `/` is the fallback. A route ending in `/` matches paths beginning with that complete prefix. My friend must use the same policy.

Return a pointer, allowing `NULL` when nothing matches. Never return a reference to a local temporary.

### Site lookup

Keep the listener's identity on the client. Port alone is insufficient when different local interfaces use the same port. The simplest mandatory implementation maps each unique resolved interface:port endpoint to its server block and rejects overlapping/duplicate listeners rather than attempting optional virtual hosting.

If we later add name-based virtual hosts, group sites under one listening socket and choose within that group using Host. Do not create one conflicting listening socket for each name.

---

## Component 3: Config

`Config` loads the file once, tokenizes it, parses blocks, validates values, and stores usable configuration. Keep it alive and unchanged while clients hold references to its blocks.

### Proposed public interface

```cpp
class Config {
public:
    void load(const std::string& filePath);
    const std::vector<ServerBlock>& getServers() const;
private:
    std::vector<ServerBlock> _servers;
};
```

The friend's proposed `getServerBlock(host, port)` needs an agreed failure/default policy and listener context. Using the exact server block associated with the accepted connection is simpler when virtual hosts are omitted.

### Parsing pipeline

```text
read file -> tokens with line numbers -> parse server/location blocks
          -> resolve defaults/inheritance -> validate -> commit config
```

1. Read the file; reject an unreadable or empty configuration.
2. Tokenize whitespace, `{`, `}`, and `;` separately, even without surrounding spaces.
3. Support `#` comments until newline. For a small first grammar, support unquoted values and explicitly reject quotes rather than half-supporting them.
4. Parse one or more `server { ... }` blocks and their `location /path { ... }` blocks.
5. Dispatch directives according to their context and expected argument count.
6. Reject malformed input with filename, line number, and a useful message.
7. Publish configuration only after the whole file passes validation.

Do not parse with “one directive per line”: a valid block may span lines or put several directives on one line. Never accept a missing semicolon just because the line ended.

### Small grammar

```text
config    := server+
server    := 'server' '{' (server_directive | location)* '}'
location  := 'location' path '{' location_directive* '}'
directive := name argument+ ';'
```

Nested locations, regex locations, and `include` files are unnecessary for this scope. Reject unknown syntax instead of silently ignoring it.

### Directives to implement

| Directive | Context in our proposed grammar | Validation |
|-----------|---------------------------------|------------|
| `listen 127.0.0.1:8080;` | server | Valid supported address, decimal port 1–65535; one per block |
| `server_name localhost;` | server, optional | One name for the first version; not mandatory virtual hosting |
| `client_max_body_size 10M;` | server and optionally location | Digits, optional agreed K/M suffix, overflow-safe conversion |
| `error_page 404 /error_pages/404.html;` | server, repeat for different codes | Valid supported error code; reject duplicate mapping |
| `root ./www;` | location | Nonempty directory setting for file-serving routes |
| `index index.html;` | location | One agreed default filename |
| `allowed_methods GET POST DELETE;` | location | Nonempty list of supported methods; reject duplicates/unknown values |
| `autoindex on;` | location | Exactly `on` or `off` |
| `redirect 301 https://example.com;` | location | Agree on supported redirect codes, e.g. 301/302, and nonempty destination |
| `upload_path ./www/uploads;` | location | Nonempty storage directory |
| `cgi_extension .py /usr/bin/python3;` | location | Extension/interpreter pair; one mandatory CGI type is sufficient |

Suggested numeric policy: K = 1024, M = 1024×1024, uppercase suffixes only; zero means no body bytes allowed. These are our choices, not mandated NGINX compatibility. Validate every character and check overflow before multiplication/addition; `atoi` alone cannot reject `8080abc` safely.

Reject duplicate singleton directives, missing `listen`, duplicate locations, directives in the wrong block, missing braces/semicolons, extra arguments, and trailing garbage. Initialize defaults and track presence separately.

### Example configuration to implement and test

This is the target syntax, not a file already present in the repository. Create the referenced directories and test files during implementation.

```nginx
server {
    listen 127.0.0.1:8080;
    client_max_body_size 10M;
    error_page 404 /error_pages/404.html;

    location / {
        root ./www;
        index index.html;
        allowed_methods GET;
        autoindex off;
    }
    location /uploads {
        root ./www/uploads;
        allowed_methods GET POST DELETE;
        upload_path ./www/uploads;
        autoindex on;
    }
    location /cgi-bin {
        root ./cgi-bin;
        allowed_methods GET POST;
        cgi_extension .py /usr/bin/python3;
    }
    location /redirect {
        redirect 301 https://example.com;
    }
}
server {
    listen 127.0.0.1:8081;
    location / {
        root ./www-second;
        index index.html;
        allowed_methods GET;
    }
}
```

Document relative filesystem paths as relative to the server's launch directory for this first version. Agree that `/error_pages/404.html` is a site-relative error-page URI resolved under the server's `/` location root, not the OS path `/error_pages/404.html`. The friend owns loading it and falling back when unavailable. A missing `/` location needs an explicit error-page fallback policy.

---

## Component 4: Client

`Client` keeps the information needed to pause and resume one connection. A TCP connection is a byte stream: one `recv()` is not necessarily one request, and one `send()` may not send the whole response.

### State machine

```text
READING_REQUEST --incomplete--> READING_REQUEST
       |
       +--parse error------------------------> WRITING_RESPONSE
       |
       v
REQUEST_COMPLETE -> BUILDING_RESPONSE
                          |
                          +-> CGI_RUNNING -> WRITING_RESPONSE
                          +----------------> WRITING_RESPONSE
                                                   |
                                      partial send +-> WRITING_RESPONSE
                                                   |
                                                   v
                                                 DONE
                                                   |
                               close OR reset for next request
```

Keep the architecture's `STATE_...` enum names so both parts compile against the same definitions.

### Required data and accessors

| Data | Why it is needed |
|------|------------------|
| Socket fd, `getFd()` | Identify connection |
| Listener/server identity, `getListenPort()` | Select the correct site; identity also distinguishes interfaces |
| State, `getState()` / `setState()` | Decide the next operation |
| Read buffer, `getReadBuffer()` | Retain bytes not consumed by parser |
| Write buffer, `getWriteBuffer()` | Hold serialized response |
| Sent offset | Resume partial sends without resending old bytes |
| Parser, `getParser()` | Incremental HTTP parsing |
| Request, `getRequest()` | Return the parser's request, avoiding a second independent copy |
| Response, `getResponse()` | Friend fills this |
| Last progress / request start / CGI deadline | Enforce bounded waits |
| CGI PID, input/output fds, input offset | Resume CGI transfers and clean up |
| Close-after-response flag | Honor request/response connection policy and errors |

Suggested ownership: `Server` owns and closes client socket fds; `Client` stores state and does not independently close the same fd. Disable accidental copying of resource-owning Server objects in C++98. If storing clients by value, ensure their copies do not close or alias owned resources unexpectedly.

`reset()` resets parser, response, write offset, request-specific CGI metadata, and state. It preserves the socket and listener identity. **Do not erase leftover read-buffer bytes:** they may belong to the next request. After reset, feed existing buffered bytes before waiting for another network event. Process requests in order.

---

## Component 5: Server

This is the coordinator. Prefer `poll()` for the first implementation: it fits the architecture and avoids maintaining several readiness backends.

### Listener setup

```text
for each configured endpoint:
    getaddrinfo (or construct validated IPv4 address)
    socket
    setsockopt(SO_REUSEADDR)
    fcntl -> non-blocking
    bind
    listen
    register fd for POLLIN
```

Check every result, release address results, and close partially created sockets if startup fails. Complete config validation before opening listeners. Do not continue with only half the required sites listening after a startup error.

On listener `POLLIN`, accept a client and explicitly make its socket non-blocking. Accepting one connection per readiness dispatch is a simple first implementation; do not copy an unbounded accept-until-error loop without deciding its error and fairness policy.

### One event loop

```text
while running:
    choose finite poll timeout, respecting upcoming deadlines
    poll(all listeners, client sockets, parent CGI pipe ends)
    dispatch each event from this readiness result:
        listener + POLLIN -> accept
        client + POLLIN -> receive a bounded amount, feed parser
        complete request -> call response builder once
        client + POLLOUT + pending output -> send remaining bytes
        CGI stdin + POLLOUT -> forward a bounded part of body
        CGI stdout + POLLIN -> collect a bounded part of output
        errors / hangups -> resolve buffered data or schedule cleanup
    check timeouts and CGI child status
    apply pending registrations/removals safely
```

Use per-dispatch work limits so one busy client cannot monopolize the loop. Do not treat one readiness event as authorization for unlimited transfer calls until `EAGAIN`; return to poll before trying further I/O that may wait.

Adding/removing entries can invalidate a `std::vector<pollfd>` and client references. Queue mutations until after dispatch, or use another clearly safe iteration strategy. When a descriptor closes, remove all its registrations and skip remaining events for it; the OS may reuse its number.

`POLLERR`, `POLLHUP`, and `POLLNVAL` need explicit handling. A hangup may arrive alongside readable bytes, especially CGI output. Do not discard buffered output merely because `POLLHUP` is set. Drain only when read readiness is present; an empty hangup can be finalized without a blind `read()`.

### Reading client data

1. Call `recv()` only after that socket reports readable readiness.
2. Positive return: append exactly that byte count, including binary data.
3. Zero: peer closed its sending direction; decide whether an already completed request can still receive a response, otherwise clean up.
4. Negative: take the documented terminal connection-error path; do not inspect `errno` to select retries.
5. Feed the parser; incomplete means wait, complete means process once, error means use the agreed error status and close policy.

Never append the receive array as a null-terminated C string. Never assume that the full request arrived in this call.

### Sending client data

```text
remaining = writeBuffer.size() - sentOffset
on writable readiness, if remaining > 0:
    sent = send(fd, bytes starting at sentOffset, remaining, flags)
    if sent > 0: advance sentOffset by exactly sent
    if sent < 0: cleanup through terminal transfer-error policy
    if sent == 0: no progress; retain state and enforce deadline
    if sentOffset == writeBuffer.size(): finish response
```

Request `POLLOUT` only with pending bytes; otherwise writable sockets will wake the loop continuously. Disable it when output is complete. Ignore `SIGPIPE` using an agreed `signal()` setup so a disconnected receiver cannot terminate the process.

### Limits and timeouts

Bound header buffers, body storage, active clients, pending response/CGI output, and waiting time. Agree with the friend on how parser errors report limits. Enforce the effective body limit during accumulation, including chunked input; checking only after the entire body is stored defeats the memory bound.

Choose limits after headers identify the selected route, or use a safe initial cap and a parser callback/API to apply the route's limit before body accumulation. The existing planned `feed()` interface alone does not describe this handoff yet.

Use a C++98-compatible wall-time approach agreed by the team, for example standard `<ctime>` facilities, and a finite poll timeout. Do not silently introduce unlisted OS timer APIs or C++11 `chrono`. Check deadlines even while other clients are continuously active. Track an absolute request/CGI deadline as well as idle progress so a slow trickle cannot keep work alive forever.

For shutdown, a signal handler should only set a `volatile sig_atomic_t` flag. Perform container changes, logging, and cleanup in normal control flow. A finite poll timeout lets the loop observe shutdown even without new clients.

### CGI: shared work, one poll loop

My friend creates the process, sets environment/working directory, and parses CGI output. We must agree on a job interface containing **both parent pipe ends**, PID, input offset, and completion state. Returning only one stdout fd is insufficient for non-blocking POST-to-CGI.

```text
request body --parent write end [POLLOUT]--> CGI stdin
CGI stdout --parent read end [POLLIN]-----> CGI output buffer
```

The parent must not write the full POST body synchronously before returning to the loop. Large input can fill a pipe while the CGI waits for its output to be drained. Monitor both directions together; close CGI stdin after all input is sent, including immediately for an empty body, so the child sees EOF.

Set the parent's transfer ends non-blocking and close unused ends on both sides. A single coordinator must remove registrations and close each fd once. Child exit is not the same as stdout being fully drained. Reap children using `waitpid(..., WNOHANG)` and arrange timeout termination/reaping with the friend. Never wait for a child synchronously in the event loop.

---

## Component 6: main.cpp and Makefile

### main.cpp

```text
validate argc (zero or one config argument)
choose explicit path or configs/default.conf
load/validate Config
install signal policy
construct Server with config lifetime guaranteed
initialize listeners
run loop
cleanup on normal exit and startup/runtime failure
```

Report config/startup failures clearly and return failure. Keep cleanup reliable during exceptions, including allocation failure as far as possible without allocating more memory in the error path. Main coordinates; parsing and socket details belong in their classes.

### Makefile

Use `NAME = webserv`, `CXX = c++`, `-Wall -Wextra -Werror -std=c++98`, and `-Iincludes`. Provide `$(NAME)`, `all`, `clean`, `fclean`, and `re`. Compile all agreed sources into objects, rebuild affected objects after header changes, and ensure a second `make` does not relink unnecessarily.

The root README is a shared mandatory deliverable: English, required italicized 42 attribution first line, Description, Instructions, and Resources including how AI was used. This guide does not replace it.

---

## What I Need From My Teammate

### Interface agreement before independent coding

| Contract | Agreement needed |
|----------|------------------|
| Parser result | Use `RequestParser::ParseResult` and its scoped enum values consistently in C++98 |
| `feed(std::string&)` | Consumes processed bytes, preserves incomplete/next-request bytes |
| Request ownership | `client.getRequest()` returns `client.getParser().getRequest()` |
| Parser errors | Specific status code or a documented default; do not turn every limit error into 400 |
| Body-limit handoff | Apply selected server/location limit before unbounded body storage |
| `buildResponse(Client&, const Config&)` | Called once per complete request; fills write buffer or starts CGI |
| `buildErrorResponse` | Agree on one signature and selected-server context for custom pages |
| Response completion | Write buffer, state, sent offset, and close-after-response flag agree |
| CGI job | Both pipe ends, PID, offsets, EOF state, registration/cleanup ownership |
| Site lookup | Listener identity is available; optional Host routing has a separate decision |
| Paths | Route-prefix removal, directory joining, and error-page resolution match |
| Keep-alive | Correct header policy, parser reset, and preserved leftover input |

### Existing mismatches to settle

The current `HttpRequest.hpp` spells the field `querystring`, while the guides use `queryString`. The current `HttpResponse.hpp` declares `headers` as a string, while the guides show a map. RequestParser, ResponseBuilder, and CgiHandler headers are currently empty. Agree on their final declarations before writing callers.

`my_part.md` suggests `config.getErrorPage(code)`, but custom error pages are per server. Use selected-server context instead of a global lookup that could serve the wrong site's page. Its CGI walkthrough also describes writing input before returning; replace that with the shared readiness-driven pipe contract above.

---

## Error Handling

| Situation | My responsibility |
|-----------|-------------------|
| Bad configuration | Explain file/line/problem; fail before listener startup |
| Port already occupied / bind fails | Explain endpoint; release all startup resources |
| Setting non-blocking mode fails | Close the new descriptor; never register a blocking one |
| `accept()` fails | Do not create a Client for fd -1; preserve the listener when recoverable |
| Receive/send failure | Remove affected connection safely; continue serving others |
| Malformed/oversized HTTP request | Ask friend to build the proper error response; apply close policy |
| Peer disconnects mid-request or mid-response | Release its buffers, socket, and any associated CGI job |
| Request/send timeout | Send an error only if appropriate and still possible; otherwise close |
| CGI timeout | Coordinate kill, pipe removal, reaping, and friend's timeout response |
| Stop signal | Exit loop and close listeners, clients, pipes, and children through shared cleanup |

Cleanup must not depend on reaching the normal end of a request. Assign exactly one owner for each resource.

---

## My Task Checklist

### Phase 1 — Configuration

- [ ] Agree on interfaces, grammar, defaults, and path rules with my friend.
- [ ] Implement and initialize LocationBlock and ServerBlock.
- [ ] Tokenize braces, semicolons, comments, and whitespace with line numbers.
- [ ] Parse all mandatory configuration features.
- [ ] Validate numbers, contexts, duplicates, missing values, and file structure.
- [ ] Resolve inherited limits without depending on directive order.
- [ ] Supply default and multi-port demonstration configurations.

### Phase 2 — Core

- [ ] Implement main and Makefile.
- [ ] Create all required listening endpoints, with complete failure cleanup.
- [ ] Make listeners and accepted sockets non-blocking.
- [ ] Implement Client state, buffers, offsets, and listener identity.
- [ ] Implement one poll loop for readable and writable descriptors.
- [ ] Integrate parser and response builder; get one real GET working.
- [ ] Handle partial input/output, disconnects, SIGPIPE, and event errors.
- [ ] Implement bounded waits and buffers.

### Phase 3 — Integration and Hardening

- [ ] Integrate both CGI pipe directions into the same poll loop.
- [ ] Coordinate CGI timeout and child cleanup.
- [ ] Confirm body-limit enforcement during parsing.
- [ ] Confirm custom errors, redirects, uploads, DELETE, autoindex, and CGI through friend's handlers.
- [ ] Implement/test agreed connection-close or persistent-connection policy; never claim keep-alive while always closing.
- [ ] Test multiple ports, slow clients, large output, and repeated disconnects.
- [ ] Check descriptor/memory cleanup and stress behavior together.
- [ ] Finish shared README and demonstration files.

---

## Testing Guide

These are tests to run **after implementation**, not tests already passed. Test tools are separate from the submitted C++ server and do not need to use its function whitelist.

### Parser-only tests first

Use a small assert-based C++98 test executable that loads fixtures and inspects resulting values. It must test the real parser, not duplicate its logic.

| Input | Expected result |
|-------|-----------------|
| Valid server with two locations | Correct endpoint, route data, and defaults |
| Two servers on different ports | Two correctly associated sites |
| Comments and compact `server{...}` syntax | Same meaning as spaced configuration |
| Missing `;` or `}` | Clear line-specific failure |
| Unknown directive / wrong context | Rejection |
| `listen -1;`, `65536`, or `8080abc` | Rejection |
| Huge body-size integer / bad suffix | Rejection without overflow |
| Duplicate listen/location/singleton | Rejection according to documented policy |
| Override before/after server body limit | Identical effective limit |
| Missing root on file-serving route | Rejection; redirect-only route still accepted |
| `/uploads` versus `/uploads-old` | Documented boundary behavior |

### Build and launch

```bash
make
make                            # should perform no unnecessary rebuild/relink
./webserv                       # default configuration
# Stop the first server before starting a second copy on the same ports.
./webserv configs/test.conf
```

Test too many arguments, an unreadable file, invalid configurations, and an occupied port. Each startup failure must release everything already acquired.

### Multiple sites and standard requests

```bash
curl --max-time 5 -v http://127.0.0.1:8080/
curl --max-time 5 -v http://127.0.0.1:8081/
curl --max-time 5 -v http://127.0.0.1:8080/nonexistent
curl --max-time 5 -v http://127.0.0.1:8080/redirect
curl --max-time 5 -v http://127.0.0.1:8080/cgi-bin/test.py
```

The ports should return distinct known files. Repeat in a browser. Upload, DELETE, body-limit, and CGI tests require the friend's handlers and matching test fixtures; diagnose transport/state failures separately from HTTP content failures.

### Partial input and concurrency

This external Python test keeps one incomplete request open while making a complete request on another connection:

```python
import socket

with socket.create_connection(("127.0.0.1", 8080), timeout=3) as slow:
    slow.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
    with socket.create_connection(("127.0.0.1", 8080), timeout=3) as fast:
        fast.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        response = bytearray()
        while True:
            data = fast.recv(4096)
            if not data:
                break
            response.extend(data)
        assert response.startswith(b"HTTP/1.1 200 "), response[:100]
    slow.sendall(b"Connection: close\r\n\r\n")
    response = bytearray()
    while True:
        data = slow.recv(4096)
        if not data:
            break
        response.extend(data)
    assert response.startswith(b"HTTP/1.1 200 "), response[:100]
```

This basic test shows that incomplete input does not block a second client. Add split-body/chunked tests with my friend and verify that configured request deadlines eventually close stalled clients.

### Partial output, CGI, and cleanup

- Request a large binary fixture through a client that reads slowly. Compare the complete received body byte-for-byte with the file; check a second client remains responsive. A small HTML page rarely exposes partial-send bugs.
- POST a body larger than pipe capacity to a CGI that also produces substantial output. Verify both directions progress and other clients remain responsive.
- Test a CGI that exits early, emits no output, and exceeds its deadline. Check child processes are reaped and pipe fds disappear.
- Repeatedly disconnect clients halfway through requests and responses. Check the server survives and its descriptor count returns near baseline.
- If persistence is supported, send two requests over one socket, including two arriving in one network write, and verify ordered complete responses. Two URLs in a curl invocation alone do not prove reuse.
- Run an available load tool, for example `siege -c 50 -t 30s http://127.0.0.1:8080/`, and check availability plus resource use. This is a smoke test, not proof of resilience.
- Run `valgrind --leak-check=full --track-fds=yes ./webserv configs/default.conf`, exercise failures and CGI, then stop cleanly. Inspect leaks and unexpected open descriptors.

---

## Three-Day Plan

### Is three days enough?

**Possible for a focused first implementation of my part if I already know C++98 and can spend about 7–9 focused hours each day. Tight for coding, understanding, integration, and reliable testing together.** This is an estimate, not a completion guarantee.

The local repository currently has no Config/Core implementation and almost all teammate implementations are empty. Completing my module is different from completing the whole working project. HTTP and CGI integration also depend on my friend's progress. If sockets, incremental parsing, and poll are all new, allow roughly a week or more rather than expecting evaluation-ready work in three days.

The architecture's four-week schedule is a team plan, not a subject deadline. Compress by using one readiness mechanism, a small strict configuration grammar, and the mandatory feature set. Defer optional name-based virtual hosts, cookies/sessions, multiple CGI types, regex routing, includes, and elaborate abstractions. Never defer correct readiness handling, validation, partial transfers, cleanup, or mandatory CGI integration.

| Day | Coding target | Understanding target | End-of-day checkpoint |
|-----|---------------|----------------------|-----------------------|
| 1, about 8 hours | Agree on contracts (1h); data classes and tokenizer (2h); parser/validation/defaults (3h); fixtures and explanation (2h) | Trace configuration text into typed settings; explain validation and route mapping | Load two sites and all required settings; reject malformed fixtures; friend can use getters |
| 2, about 8 hours | Build/main and listeners (2h); Client and poll dispatch (3h); parser/response integration and partial sends (2h); explanation/tests (1h) | Explain socket lifecycle, readiness, state, byte counts, and offsets | One real GET works end-to-end; two ports and concurrent clients work |
| 3, about 8 hours | Both CGI pipes and lifecycle (3h); timeouts/errors/cleanup and connection policy (2h); stress/failure tests and explanation (3h) | Trace a POST through CGI; explain ownership and every cleanup path | Mandatory integration exercised, no known blocking path, test results recorded |

If the HTTP handler is unavailable on Day 2, use a tiny temporary test response to validate transport, then replace it with the real integration. That checkpoint proves the core only; it does not complete the project.

If Day 1 finishes without validated config, or Day 2 finishes without an integrated GET, the three-day target is slipping. Reduce optional scope and revise the deadline instead of cutting required behavior or the final explanation/testing block.

---

## Understanding Checklist

Before calling my part finished, explain these aloud to my teammate without reading the implementation:

1. What is the difference between a listening socket and an accepted client socket?
2. Why do we need both non-blocking descriptors and poll readiness?
3. Why is one recv not one request, and how do incomplete bytes survive?
4. How does a partial send resume without repeating or losing bytes?
5. Why does registering POLLOUT on an empty connection waste CPU?
6. Which transfers require poll, and why are regular disk files exempt?
7. How does my transfer-error policy respect the subject's errno restriction?
8. How are a server block, a listening endpoint, and a location different?
9. How are defaults and inheritance resolved without depending on directive order?
10. How are overflow, duplicate directives, and malformed braces detected?
11. Who owns each socket, pipe, child, and buffer on every failure path?
12. Why can synchronous CGI input or blocking waitpid freeze every client?
13. How are large/chunked bodies limited before exhausting memory?
14. How do shutdown, idle timeouts, CGI deadlines, and leftover keep-alive bytes work?
15. Can I trace one request from config loading through accept, parse, response, send, and cleanup?

> Start with the interface agreement and a strict config parser. Aim for a real integrated GET by the end of Day 2, while reserving Day 3 for CGI, failures, and explaining the design.

# 🌐 Webserv — Project Architecture

> **42 School — HTTP/1.1 Web Server in C++98**

---

## 📋 Table of Contents

1. [High-Level Overview](#high-level-overview)
2. [Directory Structure](#directory-structure)
3. [Architecture Diagram](#architecture-diagram)
4. [Team Responsibilities](#team-responsibilities)
5. [Shared Interfaces (The Contract)](#shared-interfaces-the-contract)
6. [Part A — Configuration & Core (Teammate)](#part-a--configuration--core-teammate)
7. [Part B — Request Parsing & Response (You)](#part-b--request-parsing--response-you)
8. [Class Descriptions](#class-descriptions)
9. [Data Flow](#data-flow)
10. [Integration Points](#integration-points)
11. [Development Phases](#development-phases)

---

## High-Level Overview

The server follows the **Reactor Pattern** with a single `poll()`/`epoll()` event loop.
Every I/O operation is **non-blocking**. Each client connection is modeled as a
**state machine** that advances through states as data becomes available.

```
┌─────────────────────────────────────────────────────────────────┐
│                        WEBSERV                                  │
│                                                                 │
│  ┌──────────┐   ┌──────────────┐   ┌─────────────────────────┐ │
│  │  Config   │──▶│  Core/Server │──▶│  Request ↔ Response     │ │
│  │  Parser   │   │  (poll loop) │   │  (HTTP Processing)      │ │
│  └──────────┘   └──────────────┘   └─────────────────────────┘ │
│   Teammate          Teammate                 You                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Directory Structure

```
WEBSERV/
├── Makefile
├── configs/
│   ├── default.conf            # Default NGINX-style config
│   └── test.conf               # Test configuration
├── includes/
│   ├── Config.hpp              # [Teammate]
│   ├── ServerBlock.hpp         # [Teammate]
│   ├── LocationBlock.hpp       # [Teammate]
│   ├── Server.hpp              # [Teammate]
│   ├── Client.hpp              # [Teammate] (you both touch this)
│   ├── HttpRequest.hpp         # [You]
│   ├── HttpResponse.hpp        # [You]
│   ├── RequestParser.hpp       # [You]
│   ├── ResponseBuilder.hpp     # [You]
│   ├── CgiHandler.hpp          # [You]
│   ├── MimeTypes.hpp           # [You]
│   └── Utils.hpp               # [Shared]
├── srcs/
│   ├── main.cpp                # [Teammate]
│   ├── config/
│   │   ├── Config.cpp          # [Teammate]
│   │   ├── ServerBlock.cpp     # [Teammate]
│   │   └── LocationBlock.cpp   # [Teammate]
│   ├── core/
│   │   ├── Server.cpp          # [Teammate]
│   │   └── Client.cpp          # [Teammate]
│   ├── http/
│   │   ├── HttpRequest.cpp     # [You]
│   │   ├── HttpResponse.cpp    # [You]
│   │   ├── RequestParser.cpp   # [You]
│   │   └── ResponseBuilder.cpp # [You]
│   ├── cgi/
│   │   └── CgiHandler.cpp      # [You]
│   └── utils/
│       ├── MimeTypes.cpp       # [You]
│       └── Utils.cpp           # [Shared]
├── www/                        # Static website files
│   ├── index.html
│   ├── error_pages/
│   │   ├── 400.html
│   │   ├── 403.html
│   │   ├── 404.html
│   │   ├── 405.html
│   │   └── 500.html
│   └── uploads/
└── cgi-bin/                    # CGI scripts
    └── test.py
```

---

## Architecture Diagram

```
                         ┌─────────────────┐
                         │   Config File    │
                         │  (default.conf)  │
                         └────────┬────────┘
                                  │ parse
                         ┌────────▼────────┐
                         │     Config       │
                         │  ┌────────────┐  │
                         │  │ServerBlock[]│  │
                         │  │ ┌────────┐ │  │
                         │  │ │Location│ │  │
                         │  │ │Block[] │ │  │
                         │  │ └────────┘ │  │
                         │  └────────────┘  │
                         └────────┬────────┘
                                  │ provides config to
                         ┌────────▼────────┐
                         │     Server       │
                         │                  │
                         │  poll() loop     │◄──────── MAIN EVENT LOOP
                         │                  │
                         │  ListenSocket[]  │
                         │  Client[]        │
                         └───┬────┬────┬───┘
                   ┌─────────┘    │    └─────────┐
                   ▼              ▼              ▼
            ┌──────────┐  ┌──────────┐  ┌──────────┐
            │ Client 1 │  │ Client 2 │  │ Client N │
            │          │  │          │  │          │
            │ State:   │  │ State:   │  │ State:   │
            │ READING  │  │ WRITING  │  │ CGI_WAIT │
            └────┬─────┘  └──────────┘  └──────────┘
                 │
                 │ raw bytes
                 ▼
          ┌──────────────┐
          │RequestParser  │ ◄── Incremental state-machine parser
          │               │
          │ feed(data)    │
          │  → INCOMPLETE │
          │  → COMPLETE   │
          │  → ERROR      │
          └──────┬───────┘
                 │ produces
                 ▼
          ┌──────────────┐
          │ HttpRequest   │  method, URI, headers, body
          └──────┬───────┘
                 │
                 ▼
          ┌──────────────────┐
          │ ResponseBuilder   │ ◄── Routes request → action
          │                   │
          │ • Match Location  │ (uses Config)
          │ • Check method    │
          │ • Serve file /    │
          │   directory list /│
          │   redirect /      │
          │   CGI / error     │
          └──────┬───────────┘
                 │ produces
                 ▼
          ┌──────────────┐
          │ HttpResponse  │  status, headers, body
          │               │
          │ serialize()   │──▶ raw bytes back to Client
          └──────────────┘
```

---

## Team Responsibilities

### 🔵 Teammate — Config Parsing & Core

| Component        | Description                                              |
|------------------|----------------------------------------------------------|
| `Config`         | Parse the NGINX-style config file into C++ data structures|
| `ServerBlock`    | Represents a `server { }` block (host, port, server_name)|
| `LocationBlock`  | Represents a `location /path { }` block (routes/rules)  |
| `Server`         | The main event loop: `poll()`, accept, read, write dispatch|
| `Client`         | Represents a connected client: socket fd, state, buffers |
| `main.cpp`       | Entry point: parse args, load config, start server       |

### 🟢 You — Request Parsing & Response Generation

| Component         | Description                                              |
|-------------------|----------------------------------------------------------|
| `HttpRequest`     | Data class holding parsed request (method, URI, headers) |
| `RequestParser`   | State-machine parser: feeds raw bytes → HttpRequest      |
| `HttpResponse`    | Data class holding response (status, headers, body)      |
| `ResponseBuilder` | Logic: routes request to file/dir/CGI/redirect/error     |
| `CgiHandler`      | fork + exec CGI scripts, manage pipes                    |
| `MimeTypes`       | Map file extensions → Content-Type                       |

---

## Shared Interfaces (The Contract)

> **This is the most critical section.** These are the interfaces both
> teammates must agree on **before** coding independently.

### `Client` States (enum)

```cpp
enum ClientState {
    STATE_READING_REQUEST,   // Receiving bytes, feeding to parser
    STATE_REQUEST_COMPLETE,  // Full request parsed, ready to process
    STATE_BUILDING_RESPONSE, // ResponseBuilder is working
    STATE_CGI_RUNNING,       // Waiting for CGI child process
    STATE_WRITING_RESPONSE,  // Sending response bytes to socket
    STATE_DONE               // Connection can be closed/kept-alive
};
```

### `Client` Class — The Bridge Between Core & HTTP

```cpp
class Client {
public:
    // --- Teammate manages these ---
    int                 getFd() const;
    ClientState         getState() const;
    void                setState(ClientState state);
    std::string&        getReadBuffer();    // raw bytes from socket
    std::string&        getWriteBuffer();   // raw bytes to send

    // --- You provide these objects ---
    RequestParser&      getParser();
    HttpRequest&        getRequest();
    HttpResponse&       getResponse();

    // --- Teammate calls this in the loop ---
    void                reset();  // prepare for next request (keep-alive)
};
```

### How the Core calls your code (Integration API)

```cpp
// In Server::handleRead(Client& client):
//   1. Core reads bytes from socket into client.getReadBuffer()
//   2. Core calls:

ParseResult result = client.getParser().feed(client.getReadBuffer());

if (result == PARSE_COMPLETE) {
    client.setState(STATE_REQUEST_COMPLETE);
}
else if (result == PARSE_ERROR) {
    // Build error response (400 Bad Request)
    ResponseBuilder::buildErrorResponse(client, 400);
    client.setState(STATE_WRITING_RESPONSE);
}
// else PARSE_INCOMPLETE → return to poll loop


// In Server::handleProcess(Client& client):
//   Core calls when state == STATE_REQUEST_COMPLETE:

ResponseBuilder::buildResponse(client, config);
// This sets client.getWriteBuffer() with serialized response
// and sets state to STATE_WRITING_RESPONSE (or STATE_CGI_RUNNING)
```

---

## Part A — Configuration & Core (Teammate)

### Config Parser

Parses NGINX-like config files:

```nginx
server {
    listen 8080;
    server_name localhost;
    client_max_body_size 10M;
    error_page 404 /error_pages/404.html;

    location / {
        root /var/www/html;
        index index.html;
        allowed_methods GET POST;
        autoindex off;
    }

    location /uploads {
        root /var/www/html;
        allowed_methods GET POST DELETE;
        upload_path /var/www/html/uploads;
    }

    location /cgi-bin {
        root /var/www/cgi-bin;
        cgi_extension .py /usr/bin/python3;
        allowed_methods GET POST;
    }

    location /redirect {
        redirect 301 https://example.com;
    }
}
```

### Core Server Loop (Pseudocode)

```
load config
create listen sockets (one per server:port)
add listen sockets to poll set

while (running):
    poll(fds, timeout)
    for each ready fd:
        if fd is listen socket:
            accept new client → add to poll set
        if fd is client and POLLIN:
            read bytes into client.readBuffer
            feed to client.parser
            if request complete → process request
        if fd is client and POLLOUT:
            write bytes from client.writeBuffer
            if write complete → close or keep-alive
        if fd is CGI pipe:
            read CGI output → attach to response
```

---

## Part B — Request Parsing & Response (You)

### RequestParser — State Machine

```
States:
  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
  │ PARSING_LINE │───▶│PARSING_HEADER│───▶│ PARSING_BODY │───▶ COMPLETE
  │  (method,    │    │  (key:value  │    │  (Content-    │
  │   URI, ver)  │    │   pairs)     │    │   Length or   │
  └──────────────┘    └──────────────┘    │   chunked)   │
                                          └──────────────┘
```

```cpp
class RequestParser {
public:
    enum ParseResult {
        PARSE_INCOMPLETE,
        PARSE_COMPLETE,
        PARSE_ERROR
    };

    RequestParser();
    ~RequestParser();
    void            reset();
    ParseResult     feed(std::string& buffer);
    HttpRequest&    getRequest();

private:
    enum State {
        STATE_REQUEST_LINE,
        STATE_HEADERS,
        STATE_BODY,
        STATE_CHUNKED_BODY,
        STATE_COMPLETE,
        STATE_ERROR
    };

    State           _state;
    HttpRequest     _request;

    ParseResult     _parseRequestLine(std::string& buffer);
    ParseResult     _parseHeaders(std::string& buffer);
    ParseResult     _parseBody(std::string& buffer);
    ParseResult     _parseChunkedBody(std::string& buffer);
    bool            _validateMethod(const std::string& method);
    bool            _validateURI(const std::string& uri);
    bool            _validateVersion(const std::string& version);
};
```

### HttpRequest — Data Class

```cpp
class HttpRequest {
public:
    std::string                             method;     // GET, POST, DELETE
    std::string                             uri;        // /path/to/resource
    std::string                             queryString;// ?key=value&...
    std::string                             version;    // HTTP/1.1
    std::map<std::string, std::string>      headers;
    std::vector<char>                       body;       // vector<char> for binary safety
    size_t                                  contentLength;
    bool                                    isChunked;

    void reset();
    std::string getHeader(const std::string& key) const;
};
```

### HttpResponse — Data Class

```cpp
class HttpResponse {
public:
    int                                     statusCode;
    std::string                             statusText;
    std::map<std::string, std::string>      headers;
    std::vector<char>                       body;

    void        setStatus(int code);
    void        setHeader(const std::string& key, const std::string& value);
    void        setBody(const std::string& content);
    void        setBody(const std::vector<char>& content);
    std::string serialize() const;  // → "HTTP/1.1 200 OK\r\n..."
    void        reset();
};
```

### ResponseBuilder — Routing & Logic

```cpp
class ResponseBuilder {
public:
    static void buildResponse(Client& client, const Config& config);
    static void buildErrorResponse(Client& client, int statusCode);

private:
    // Step 1: Find matching server block (by Host header + port)
    static const ServerBlock&   _matchServer(const HttpRequest& req,
                                             const Config& config,
                                             int listenPort);

    // Step 2: Find matching location block
    static const LocationBlock& _matchLocation(const HttpRequest& req,
                                               const ServerBlock& server);

    // Step 3: Check if method is allowed
    static bool _isMethodAllowed(const HttpRequest& req,
                                 const LocationBlock& loc);

    // Step 4: Route to handler
    static void _handleGET(Client& client, const LocationBlock& loc);
    static void _handlePOST(Client& client, const LocationBlock& loc);
    static void _handleDELETE(Client& client, const LocationBlock& loc);
    static void _handleRedirect(Client& client, const LocationBlock& loc);
    static void _handleAutoindex(Client& client, const std::string& path);

    // Helpers
    static std::string  _resolvePath(const std::string& uri,
                                     const LocationBlock& loc);
    static std::string  _readFile(const std::string& path);
    static bool         _fileExists(const std::string& path);
    static bool         _isDirectory(const std::string& path);
};
```

### CgiHandler

```cpp
class CgiHandler {
public:
    static bool     isCgi(const std::string& uri, const LocationBlock& loc);
    static int      execute(Client& client, const LocationBlock& loc,
                            const Config& config);
    // Returns the pipe fd to monitor in poll()

    static bool     readOutput(int pipeFd, Client& client);
    // Called when pipe fd is ready for reading

private:
    static std::map<std::string, std::string>
        _buildEnvVars(const HttpRequest& req, const LocationBlock& loc);
    // QUERY_STRING, REQUEST_METHOD, CONTENT_TYPE, etc.
};
```

### MimeTypes

```cpp
class MimeTypes {
public:
    static std::string getType(const std::string& filepath);
    // ".html" → "text/html"
    // ".css"  → "text/css"
    // ".js"   → "application/javascript"
    // ".png"  → "image/png"
    // etc.
};
```

---

## Data Flow

### Complete Request Lifecycle

```
1. Client connects
   └─▶ Server::accept() → new Client(fd) → state = READING_REQUEST

2. Data arrives (POLLIN)
   └─▶ Server::handleRead()
       ├─ recv(fd, buffer)
       └─ parser.feed(buffer)
           ├─ INCOMPLETE → back to poll()
           ├─ ERROR → buildErrorResponse(400) → state = WRITING
           └─ COMPLETE → state = REQUEST_COMPLETE

3. Process request
   └─▶ ResponseBuilder::buildResponse(client, config)
       ├─ Match server block (Host header)
       ├─ Match location block (URI prefix)
       ├─ Check allowed methods → 405 if not allowed
       ├─ Check client_max_body_size → 413 if too large
       ├─ Route to handler:
       │   ├─ Redirect? → 301/302 response
       │   ├─ CGI? → CgiHandler::execute() → state = CGI_RUNNING
       │   ├─ Directory + autoindex? → generate listing
       │   ├─ Directory + index? → serve index file
       │   ├─ File exists? → serve file with MIME type
       │   └─ Not found? → 404 error page
       └─ response.serialize() → client.writeBuffer

4. Send response (POLLOUT)
   └─▶ Server::handleWrite()
       ├─ send(fd, writeBuffer)
       ├─ If partial write → keep in WRITING state
       └─ If complete:
           ├─ Connection: keep-alive → client.reset() → READING
           └─ Connection: close → remove client

5. CGI flow (if applicable)
   └─▶ CgiHandler::execute()
       ├─ pipe() + fork()
       ├─ Child: execve(cgi_script)
       ├─ Parent: add pipe_fd to poll set
       └─ When pipe ready → readOutput() → parse CGI headers
           → build response → state = WRITING
```

---

## Integration Points

### Where your code meets teammate's code

| Integration Point | Who calls whom | Data exchanged |
|---|---|---|
| **Parser invocation** | Core calls `parser.feed(buffer)` | Raw bytes → ParseResult |
| **Response trigger** | Core calls `ResponseBuilder::buildResponse()` | HttpRequest + Config → HttpResponse |
| **Config access** | ResponseBuilder reads `Config` | ServerBlock, LocationBlock lookups |
| **Write buffer** | You fill `client.writeBuffer` | Serialized HTTP response string |
| **CGI pipe** | You return pipe fd → Core adds to `poll()` | Core monitors fd, calls your read handler |
| **Client reset** | Core calls `client.reset()` which calls your `parser.reset()` | Clear state for keep-alive |

### Agreement Checklist

Before coding independently, agree on:

- [ ] Config class API — how to query server/location blocks
- [ ] Client class interface — buffer access, state management
- [ ] CGI pipe fd registration — how to add/remove from poll set
- [ ] Error page paths — how custom error pages are stored in config
- [ ] `client_max_body_size` — where the check happens (parser or response builder)

---

## Development Phases

### Phase 1 — Foundation (Week 1)
| Task | Owner |
|------|-------|
| Set up Makefile, directory structure | Both |
| Define shared interfaces (hpp files) | Both |
| Parse basic config (server, listen, root) | Teammate |
| Parse HTTP GET request line + headers | You |
| Basic `poll()` loop, accept connections | Teammate |
| Serve a static HTML file (200 response) | You |

### Phase 2 — Core Features (Week 2)
| Task | Owner |
|------|-------|
| Full config parsing (all directives) | Teammate |
| Multiple server blocks, virtual hosts | Teammate |
| POST request body parsing | You |
| DELETE method handling | You |
| Custom error pages | You |
| Directory listing (autoindex) | You |
| `client_max_body_size` enforcement | Both |

### Phase 3 — CGI & Advanced (Week 3)
| Task | Owner |
|------|-------|
| CGI pipe integration in poll loop | Teammate |
| CGI fork/exec + env vars | You |
| CGI output parsing (headers + body) | You |
| Chunked transfer encoding | You |
| File upload via POST | You |
| HTTP redirections | You |
| Multiple ports / server names routing | Teammate |

### Phase 4 — Hardening & Testing (Week 4)
| Task | Owner |
|------|-------|
| Stress testing (siege, ab) | Both |
| Edge cases (partial reads/writes, timeouts) | Both |
| Memory leak checks (valgrind) | Both |
| Compare behavior with NGINX | Both |
| Subject checklist walkthrough | Both |

---

## Key Rules to Remember

1. **Never block.** Every `read()`/`write()` must go through `poll()` first.
2. **Never check `errno` after read/write** — the subject forbids it.
3. **Handle partial data** — your parser must buffer and resume.
4. **No external libraries** — no Boost, no third-party HTTP parsers.
5. **C++98 only** — no `auto`, no `nullptr`, no range-for, no `std::to_string`.
6. **Only `fork()` for CGI** — not for handling connections.
7. **The server must never crash** — handle all edge cases gracefully.

---

## Useful Status Codes to Implement

| Code | Text | When |
|------|------|------|
| 200 | OK | Successful GET/POST |
| 201 | Created | Successful file upload |
| 204 | No Content | Successful DELETE |
| 301 | Moved Permanently | Config redirect |
| 400 | Bad Request | Malformed HTTP request |
| 403 | Forbidden | No permission to access |
| 404 | Not Found | Resource doesn't exist |
| 405 | Method Not Allowed | Method not in allowed_methods |
| 413 | Payload Too Large | Body > client_max_body_size |
| 500 | Internal Server Error | CGI crash, unexpected error |

---

> **💡 Tip:** Start by getting a basic GET request → static file response
> working end-to-end. Then layer on features incrementally. Test with
> `curl`, a browser, and eventually `siege` for stress testing.

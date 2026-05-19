# 🟢 My Part — HTTP Request Parsing & Response Generation

> Everything I need to build for the 42 webserv project.

---

## 📋 Table of Contents

1. [Overview](#overview)
2. [Files I Own](#files-i-own)
3. [Component 1: RequestParser](#component-1-requestparser)
4. [Component 2: HttpRequest](#component-2-httprequest)
5. [Component 3: HttpResponse](#component-3-httpresponse)
6. [Component 4: ResponseBuilder](#component-4-responsebuilder)
7. [Component 5: CgiHandler](#component-5-cgihandler)
8. [Component 6: MimeTypes](#component-6-mimetypes)
9. [Error Handling](#error-handling)
10. [What I Need From My Teammate](#what-i-need-from-my-teammate)
11. [My Task Checklist](#my-task-checklist)
12. [Testing Guide](#testing-guide)

---

## Overview

My responsibility covers everything that happens **after** raw bytes arrive from the socket and **before** the serialized response goes back. In short:

```
Raw bytes in ──▶ [RequestParser] ──▶ [HttpRequest] ──▶ [ResponseBuilder] ──▶ [HttpResponse] ──▶ Raw bytes out
                                                             │
                                                       [CgiHandler]
                                                       [MimeTypes]
```

---

## Files I Own

```
includes/
├── HttpRequest.hpp
├── HttpResponse.hpp
├── RequestParser.hpp
├── ResponseBuilder.hpp
├── CgiHandler.hpp
└── MimeTypes.hpp

srcs/
├── http/
│   ├── HttpRequest.cpp
│   ├── HttpResponse.cpp
│   ├── RequestParser.cpp
│   └── ResponseBuilder.cpp
├── cgi/
│   └── CgiHandler.cpp
└── utils/
    └── MimeTypes.cpp

www/
├── error_pages/
│   ├── 400.html
│   ├── 403.html
│   ├── 404.html
│   ├── 405.html
│   ├── 413.html
│   └── 500.html
└── (static test files for dev)
```

---

## Component 1: RequestParser

The parser is a **state machine** that consumes raw bytes incrementally. It never blocks — if the data is incomplete, it returns `PARSE_INCOMPLETE` and waits for more bytes from the next `poll()` cycle.

### State Machine Diagram

```
    ┌────────────────────┐
    │ STATE_REQUEST_LINE │ ◄── Initial state
    │                    │
    │ Looking for:       │
    │ "GET /path HTTP/1.1│\r\n"
    └────────┬───────────┘
             │ found \r\n
             ▼
    ┌────────────────────┐
    │   STATE_HEADERS    │
    │                    │
    │ Looking for:       │
    │ "Key: Value\r\n"   │
    │ until empty "\r\n" │
    └────────┬───────────┘
             │ found \r\n\r\n
             ▼
       ┌─────────────┐
       │ Has body?    │
       │             │
       ├─ No ────────┼──▶ PARSE_COMPLETE
       │             │
       ├─ Content-   │
       │  Length ─────┼──▶ STATE_BODY
       │             │
       └─ Transfer-  │
          Encoding:  │
          chunked ───┼──▶ STATE_CHUNKED_BODY
                     │
    ┌────────────────┘
    │
    ▼
    ┌────────────────────┐
    │    STATE_BODY      │
    │                    │
    │ Read exactly       │
    │ Content-Length bytes│
    └────────┬───────────┘
             │ all bytes read
             ▼
        PARSE_COMPLETE
```

### Header File

```cpp
// RequestParser.hpp
#ifndef REQUESTPARSER_HPP
# define REQUESTPARSER_HPP

# include "HttpRequest.hpp"
# include <string>

class RequestParser {
public:
    enum ParseResult {
        PARSE_INCOMPLETE,   // Need more data
        PARSE_COMPLETE,     // Full request parsed
        PARSE_ERROR         // Malformed request
    };

    RequestParser();
    ~RequestParser();

    // Feed raw bytes from the socket buffer.
    // Consumes processed bytes from `buffer`.
    ParseResult     feed(std::string& buffer);

    HttpRequest&    getRequest();
    void            reset();

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
    size_t          _bodyBytesRead;

    // Sub-parsers (each returns a ParseResult)
    ParseResult     _parseRequestLine(std::string& buffer);
    ParseResult     _parseHeaders(std::string& buffer);
    ParseResult     _parseBody(std::string& buffer);
    ParseResult     _parseChunkedBody(std::string& buffer);

    // Validators
    bool            _isValidMethod(const std::string& method);
    bool            _isValidURI(const std::string& uri);
    bool            _isValidVersion(const std::string& version);

    // Utilities
    std::string     _trim(const std::string& str);
    std::string     _toLower(const std::string& str);
};

#endif
```

### Implementation Details

#### `_parseRequestLine`

```
Input:  "GET /index.html?q=hello HTTP/1.1\r\n..."
Output: method = "GET"
        uri = "/index.html"
        queryString = "q=hello"
        version = "HTTP/1.1"
```

- Find `\r\n` in buffer — if not found → `INCOMPLETE`
- Split line by spaces — must have exactly 3 tokens
- Validate method (GET, POST, DELETE only)
- Split URI at `?` to extract query string
- Decode percent-encoding in URI (`%20` → space)
- Validate version is `HTTP/1.1`
- Consume the line from buffer
- Transition to `STATE_HEADERS`

#### `_parseHeaders`

```
Input:  "Host: localhost:8080\r\nContent-Type: text/html\r\n\r\n..."
Output: headers["host"] = "localhost:8080"
        headers["content-type"] = "text/html"
```

- Loop: find `\r\n` in buffer
  - If line is empty (`\r\n\r\n`) → headers done
  - Split at first `:` → key (lowercased, trimmed) + value (trimmed)
  - Store in `_request.headers` map
- After headers done:
  - Check `Content-Length` header → set `_request.contentLength`
  - Check `Transfer-Encoding: chunked` → set `_request.isChunked`
  - If method is GET/DELETE and no body → `COMPLETE`
  - If `Content-Length > 0` → `STATE_BODY`
  - If chunked → `STATE_CHUNKED_BODY`

#### `_parseBody`

- Append available bytes to `_request.body`
- Track `_bodyBytesRead`
- When `_bodyBytesRead >= _request.contentLength` → `COMPLETE`
- Otherwise → `INCOMPLETE`

#### `_parseChunkedBody`

```
Chunked format:
  "5\r\n"         ← chunk size in hex
  "Hello\r\n"     ← chunk data
  "6\r\n"
  "World!\r\n"
  "0\r\n"         ← final chunk (size 0)
  "\r\n"          ← end
```

- Parse chunk size (hex) from buffer
- Read that many bytes into body
- Repeat until chunk size is `0`
- Then → `COMPLETE`

### Edge Cases to Handle

| Case | Action |
|------|--------|
| Header line > 8192 bytes | Return `PARSE_ERROR` (414) |
| Total headers > 100 | Return `PARSE_ERROR` (431) |
| Unknown HTTP method | Return `PARSE_ERROR` (501) |
| Missing Host header | Return `PARSE_ERROR` (400) |
| Content-Length is negative or non-numeric | Return `PARSE_ERROR` (400) |
| URI contains `../` (path traversal) | Sanitize or return `PARSE_ERROR` (400) |
| Invalid percent encoding | Return `PARSE_ERROR` (400) |

---

## Component 2: HttpRequest

Simple data container — no logic, just storage.

```cpp
// HttpRequest.hpp
#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <string>
# include <map>
# include <vector>

class HttpRequest {
public:
    // Request line
    std::string                         method;       // "GET", "POST", "DELETE"
    std::string                         uri;          // "/path/to/resource"
    std::string                         queryString;  // "key=val&key2=val2"
    std::string                         version;      // "HTTP/1.1"

    // Headers (all keys stored lowercase)
    std::map<std::string, std::string>  headers;

    // Body (vector<char> for binary safety — images, uploads)
    std::vector<char>                   body;
    size_t                              contentLength;
    bool                                isChunked;

    HttpRequest();
    ~HttpRequest();

    void        reset();
    std::string getHeader(const std::string& key) const;
    bool        hasHeader(const std::string& key) const;
    bool        keepAlive() const;
};

#endif
```

### Key Methods

- **`getHeader(key)`** — Returns the value or empty string. Key is case-insensitive (stored lowercase).
- **`keepAlive()`** — Returns `true` if `Connection: keep-alive` or HTTP/1.1 default (no `Connection: close`).

---

## Component 3: HttpResponse

Data container + serialization.

```cpp
// HttpResponse.hpp
#ifndef HTTPRESPONSE_HPP
# define HTTPRESPONSE_HPP

# include <string>
# include <map>
# include <vector>

class HttpResponse {
public:
    int                                     statusCode;
    std::string                             statusText;
    std::map<std::string, std::string>      headers;
    std::vector<char>                       body;

    HttpResponse();
    ~HttpResponse();

    void        setStatus(int code);           // Auto-sets statusText
    void        setHeader(const std::string& key, const std::string& value);
    void        setBody(const std::string& content);
    void        setBody(const std::vector<char>& content);
    std::string serialize() const;             // Full HTTP response string
    void        reset();

private:
    static std::string _getStatusText(int code);
};

#endif
```

### `serialize()` Output Example

```http
HTTP/1.1 200 OK\r\n
Content-Type: text/html\r\n
Content-Length: 45\r\n
Connection: keep-alive\r\n
\r\n
<html><body><h1>Hello!</h1></body></html>
```

### `_getStatusText` Mapping

```cpp
std::string HttpResponse::_getStatusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default:  return "Unknown";
    }
}
```

---

## Component 4: ResponseBuilder

This is the **brain** — it takes a parsed request, consults the config, and produces the response.

```cpp
// ResponseBuilder.hpp
#ifndef RESPONSEBUILDER_HPP
# define RESPONSEBUILDER_HPP

# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "CgiHandler.hpp"
# include "MimeTypes.hpp"

// Forward declarations (teammate's classes)
class Client;
class Config;
class ServerBlock;
class LocationBlock;

class ResponseBuilder {
public:
    // Main entry point — called by the core Server loop
    static void buildResponse(Client& client, const Config& config);

    // Error response helper — also called directly for parse errors
    static void buildErrorResponse(Client& client, int statusCode,
                                   const Config* config = NULL);

private:
    // --- Routing pipeline ---
    static const ServerBlock*   _matchServer(const HttpRequest& req,
                                             const Config& config,
                                             int listenPort);
    static const LocationBlock* _matchLocation(const std::string& uri,
                                               const ServerBlock& server);

    // --- Method handlers ---
    static void _handleGET(Client& client, const LocationBlock& loc,
                           const ServerBlock& server);
    static void _handlePOST(Client& client, const LocationBlock& loc,
                            const ServerBlock& server);
    static void _handleDELETE(Client& client, const LocationBlock& loc,
                              const ServerBlock& server);

    // --- Special handlers ---
    static void _handleRedirect(Client& client, const LocationBlock& loc);
    static void _handleAutoindex(Client& client, const std::string& dirPath,
                                 const std::string& uri);
    static void _handleCGI(Client& client, const LocationBlock& loc,
                           const ServerBlock& server);

    // --- File utilities ---
    static std::string          _resolvePath(const std::string& uri,
                                             const LocationBlock& loc);
    static bool                 _fileExists(const std::string& path);
    static bool                 _isDirectory(const std::string& path);
    static std::vector<char>    _readFile(const std::string& path);
};

#endif
```

### `buildResponse` — Decision Flowchart

```
buildResponse(client, config)
│
├─ 1. Match ServerBlock by Host header + port
│     └─ No match? → use default server for that port
│
├─ 2. Match LocationBlock by URI prefix (longest match wins)
│     └─ No match? → 404
│
├─ 3. Check if redirect is configured
│     └─ Yes? → 301/302 response, DONE
│
├─ 4. Check if method is allowed
│     └─ Not allowed? → 405 (include Allow header)
│
├─ 5. Check client_max_body_size
│     └─ Body too large? → 413
│
├─ 6. Is it a CGI request? (check extension like .py, .php)
│     └─ Yes? → CgiHandler::execute(), return (state = CGI_RUNNING)
│
├─ 7. Route by method:
│     │
│     ├─ GET:
│     │   ├─ Resolve file path from URI + location root
│     │   ├─ Is directory?
│     │   │   ├─ Has index file? → serve index
│     │   │   ├─ Autoindex on? → generate directory listing
│     │   │   └─ Neither? → 403
│     │   ├─ File exists? → serve file (set Content-Type via MimeTypes)
│     │   └─ Not found? → 404
│     │
│     ├─ POST:
│     │   ├─ Has upload_path configured? → save file, return 201
│     │   └─ No upload_path? → 403 or pass to CGI
│     │
│     └─ DELETE:
│         ├─ File exists? → delete file, return 204
│         └─ Not found? → 404
│
└─ 8. Serialize response → client.writeBuffer
       Set state → STATE_WRITING_RESPONSE
```

### Location Matching Algorithm

```
URI: /uploads/photos/cat.jpg

Locations in config:
  /              ← matches (prefix length 1)
  /uploads       ← matches (prefix length 8)  ← WINNER (longest)
  /api           ← no match

Algorithm: iterate all locations, keep the one with the longest
           matching prefix.
```

### Autoindex HTML Generation

When `autoindex on` and the URI points to a directory with no index file:

```html
<html>
<head><title>Index of /uploads/</title></head>
<body>
<h1>Index of /uploads/</h1>
<hr>
<pre>
<a href="../">../</a>
<a href="photo1.jpg">photo1.jpg</a>          2024-01-15 14:30    1.2M
<a href="photo2.png">photo2.png</a>          2024-01-16 09:00    856K
<a href="subdir/">subdir/</a>               2024-01-17 11:45       -
</pre>
<hr>
</body>
</html>
```

Use `opendir()` / `readdir()` / `stat()` to list directory contents.

---

## Component 5: CgiHandler

Executes CGI scripts (Python, PHP, etc.) via `fork()` + `execve()`.

```cpp
// CgiHandler.hpp
#ifndef CGIHANDLER_HPP
# define CGIHANDLER_HPP

# include "HttpRequest.hpp"
# include <string>
# include <map>

class Client;
class LocationBlock;

class CgiHandler {
public:
    // Check if the request URI matches a CGI extension
    static bool isCgi(const std::string& uri, const LocationBlock& loc);

    // Execute CGI script. Returns pipe fd for poll() monitoring.
    // Sets client state to CGI_RUNNING.
    static int  execute(Client& client, const LocationBlock& loc);

    // Called when pipe fd is ready (POLLIN). Reads CGI output.
    // Returns true when CGI output is fully read.
    static bool readOutput(int pipeFd, Client& client);

    // Parse CGI output into headers + body for HttpResponse
    static void parseCgiOutput(Client& client);

private:
    // Build the environment variables array for execve
    static char**   _buildEnvp(const HttpRequest& req,
                               const LocationBlock& loc);
    static void     _freeEnvp(char** envp);

    static std::string _getScriptPath(const std::string& uri,
                                      const LocationBlock& loc);
    static std::string _getInterpreter(const std::string& uri,
                                       const LocationBlock& loc);
};

#endif
```

### CGI Execution Flow

```
execute(client, location)
│
├─ 1. Resolve script path:  /var/www/cgi-bin/script.py
├─ 2. Get interpreter:      /usr/bin/python3
│
├─ 3. Create pipes:
│     ├─ pipe_in[2]   (server → CGI stdin)
│     └─ pipe_out[2]  (CGI stdout → server)
│
├─ 4. fork()
│     │
│     ├─ CHILD:
│     │   ├─ dup2(pipe_in[0], STDIN)
│     │   ├─ dup2(pipe_out[1], STDOUT)
│     │   ├─ close unused pipe ends
│     │   ├─ Build envp (environment variables)
│     │   ├─ execve(interpreter, {interpreter, script}, envp)
│     │   └─ exit(1) if execve fails
│     │
│     └─ PARENT:
│         ├─ close unused pipe ends
│         ├─ Write request body to pipe_in[1] (if POST)
│         ├─ close pipe_in[1]
│         ├─ Store pipe_out[0] fd + child PID in client
│         └─ Return pipe_out[0] → core adds to poll()
│
└─ 5. State = CGI_RUNNING
```

### Required CGI Environment Variables

| Variable | Value | Example |
|----------|-------|---------|
| `REQUEST_METHOD` | From request | `GET` |
| `QUERY_STRING` | From URI after `?` | `name=john&age=25` |
| `CONTENT_TYPE` | From request header | `application/x-www-form-urlencoded` |
| `CONTENT_LENGTH` | Body size | `42` |
| `SCRIPT_NAME` | Script path in URI | `/cgi-bin/script.py` |
| `SCRIPT_FILENAME` | Absolute file path | `/var/www/cgi-bin/script.py` |
| `PATH_INFO` | Extra path after script | `/extra/path` |
| `SERVER_NAME` | From Host header | `localhost` |
| `SERVER_PORT` | Listen port | `8080` |
| `SERVER_PROTOCOL` | HTTP version | `HTTP/1.1` |
| `GATEWAY_INTERFACE` | CGI version | `CGI/1.1` |
| `HTTP_COOKIE` | From Cookie header | `session=abc123` |

### CGI Output Parsing

CGI scripts output headers followed by a blank line then the body:

```
Content-Type: text/html\r\n
Status: 200 OK\r\n
\r\n
<html><body>Hello from CGI!</body></html>
```

- Parse headers from CGI output (especially `Content-Type` and `Status`)
- If `Status` header exists → use that as response status code
- Otherwise default to `200 OK`
- Everything after `\r\n\r\n` is the response body

---

## Component 6: MimeTypes

Maps file extensions to HTTP Content-Type values.

```cpp
// MimeTypes.hpp
#ifndef MIMETYPES_HPP
# define MIMETYPES_HPP

# include <string>

class MimeTypes {
public:
    static std::string getType(const std::string& filepath);

private:
    static void _initMap();  // Lazy initialization
};

#endif
```

### Extension Map

| Extension | Content-Type |
|-----------|-------------|
| `.html`, `.htm` | `text/html` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.json` | `application/json` |
| `.xml` | `application/xml` |
| `.txt` | `text/plain` |
| `.png` | `image/png` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.gif` | `image/gif` |
| `.svg` | `image/svg+xml` |
| `.ico` | `image/x-icon` |
| `.pdf` | `application/pdf` |
| `.zip` | `application/zip` |
| `.mp4` | `video/mp4` |
| `.webp` | `image/webp` |
| (default) | `application/octet-stream` |

---

## Error Handling

### Error Response Builder

```cpp
void ResponseBuilder::buildErrorResponse(Client& client, int code,
                                         const Config* config)
{
    HttpResponse& res = client.getResponse();
    res.setStatus(code);

    // 1. Try custom error page from config
    if (config) {
        std::string customPage = config->getErrorPage(code);
        if (!customPage.empty() && _fileExists(customPage)) {
            res.setBody(_readFile(customPage));
            res.setHeader("Content-Type", "text/html");
            // serialize and return
            return;
        }
    }

    // 2. Fallback: generate a default HTML error page
    std::string html =
        "<html><head><title>" + intToStr(code) + " " +
        res.statusText + "</title></head>"
        "<body><center><h1>" + intToStr(code) + " " +
        res.statusText + "</h1></center>"
        "<hr><center>webserv</center></body></html>";

    res.setBody(html);
    res.setHeader("Content-Type", "text/html");
}
```

### Error Scenarios I Must Handle

| Scenario | Code | Where |
|----------|------|-------|
| Malformed request line | 400 | RequestParser |
| Missing Host header | 400 | RequestParser |
| URI not found | 404 | ResponseBuilder |
| Method not in allowed_methods | 405 | ResponseBuilder |
| Body > client_max_body_size | 413 | ResponseBuilder |
| Unsupported method (PUT, PATCH) | 501 | RequestParser |
| CGI script crashes | 500 | CgiHandler |
| File permission denied | 403 | ResponseBuilder |
| CGI timeout | 504 | CgiHandler |
| Directory with no index & autoindex off | 403 | ResponseBuilder |

---

## What I Need From My Teammate

These are the interfaces/data I need from the Config and Core parts:

### From Config

```cpp
// I need to be able to do:
const ServerBlock& server = config.getServerBlock(host, port);
const LocationBlock& loc  = server.matchLocation(uri);

// LocationBlock must expose:
loc.getRoot();              // std::string - root directory
loc.getIndex();             // std::string - default index file
loc.getAllowedMethods();    // std::vector<std::string>
loc.getAutoindex();         // bool
loc.getRedirectCode();      // int (0 if no redirect)
loc.getRedirectURL();       // std::string
loc.getCgiExtension();      // std::map<std::string, std::string> ext→interpreter
loc.getUploadPath();        // std::string
loc.getClientMaxBodySize(); // size_t (in bytes)
```

### From Core (Client class)

```cpp
// I need access to:
client.getRequest();        // HttpRequest& — I fill this via parser
client.getResponse();       // HttpResponse& — I fill this
client.getParser();         // RequestParser& — core calls feed()
client.getReadBuffer();     // std::string& — raw bytes from socket
client.getWriteBuffer();    // std::string& — I put serialized response here
client.setState(state);     // Set next state after processing
client.getListenPort();     // int — which port this client connected on
```

---

## My Task Checklist

### Phase 1 — Basic GET Working
- [ ] `HttpRequest` class (data container)
- [ ] `HttpResponse` class with `serialize()`
- [ ] `RequestParser` — parse request line
- [ ] `RequestParser` — parse headers
- [ ] `MimeTypes` — basic extension map
- [ ] `ResponseBuilder` — serve a static file for GET
- [ ] Default error page generation (404, 400)

### Phase 2 — Full HTTP Support
- [ ] `RequestParser` — parse Content-Length body
- [ ] `RequestParser` — parse chunked body
- [ ] `ResponseBuilder` — POST file upload
- [ ] `ResponseBuilder` — DELETE file
- [ ] `ResponseBuilder` — redirections (301, 302)
- [ ] `ResponseBuilder` — autoindex directory listing
- [ ] `ResponseBuilder` — method not allowed (405)
- [ ] `ResponseBuilder` — payload too large (413)
- [ ] Custom error pages from config

### Phase 3 — CGI
- [ ] `CgiHandler::isCgi()` — detect CGI requests
- [ ] `CgiHandler::execute()` — fork + pipe + execve
- [ ] `CgiHandler::readOutput()` — read pipe output
- [ ] `CgiHandler::parseCgiOutput()` — parse CGI headers/body
- [ ] CGI environment variables
- [ ] CGI POST body forwarding (via stdin pipe)
- [ ] CGI timeout handling

### Phase 4 — Hardening
- [ ] URI percent-decoding
- [ ] Path traversal prevention (`../`)
- [ ] Request size limits
- [ ] Keep-alive support
- [ ] Proper `Connection` header handling
- [ ] All edge cases from the table above

---

## Testing Guide

### Manual Tests with curl

```bash
# Basic GET
curl -v http://localhost:8080/
curl -v http://localhost:8080/index.html

# 404
curl -v http://localhost:8080/nonexistent

# POST upload
curl -v -X POST -d "hello world" http://localhost:8080/uploads/test.txt

# DELETE
curl -v -X DELETE http://localhost:8080/uploads/test.txt

# Method not allowed
curl -v -X PUT http://localhost:8080/

# Large body (should trigger 413)
curl -v -X POST -d @largefile.bin http://localhost:8080/uploads/big.txt

# CGI
curl -v http://localhost:8080/cgi-bin/test.py?name=john

# POST to CGI
curl -v -X POST -d "name=john" http://localhost:8080/cgi-bin/test.py

# Chunked transfer
curl -v -H "Transfer-Encoding: chunked" -d @file.txt http://localhost:8080/uploads/

# Keep-alive (multiple requests on same connection)
curl -v --http1.1 http://localhost:8080/ http://localhost:8080/index.html

# Directory listing
curl -v http://localhost:8080/uploads/

# Redirect
curl -v -L http://localhost:8080/redirect
```

### Test CGI Script (`cgi-bin/test.py`)

```python
#!/usr/bin/env python3
import os
import sys

print("Content-Type: text/html")
print("Status: 200 OK")
print()
print("<html><body>")
print("<h1>CGI Works!</h1>")
print("<p>Method: {}</p>".format(os.environ.get("REQUEST_METHOD", "N/A")))
print("<p>Query: {}</p>".format(os.environ.get("QUERY_STRING", "N/A")))

if os.environ.get("REQUEST_METHOD") == "POST":
    body = sys.stdin.read(int(os.environ.get("CONTENT_LENGTH", 0)))
    print("<p>Body: {}</p>".format(body))

print("</body></html>")
```

### Stress Test

```bash
# Install siege
sudo apt install siege

# Run stress test
siege -c 100 -t 30s http://localhost:8080/

# Check for memory leaks
valgrind --leak-check=full ./webserv configs/default.conf
```

---

> **Start with Phase 1.** Get a simple GET → static file working end-to-end
> with your teammate's core loop. Everything else builds on top of that.

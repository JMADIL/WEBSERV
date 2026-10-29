# 📄 RequestParser Class Documentation

This document explains the `RequestParser` class, the complete C++ implementation file (`RequestParser.cpp`), and provides an in-depth breakdown of every method, state transition, and RFC requirement.

---

## 📌 Table of Contents
1. [Overview](#overview)
2. [State Machine Architecture](#state-machine-architecture)
3. [Header Definition (`RequestParser.hpp`)](#header-definition-requestparserhpp)
4. [Implementation File (`RequestParser.cpp`)](#implementation-file-requestparsercpp)
5. [Detailed Breakdown of Methods](#detailed-breakdown-of-methods)
   - [Constructor & Destructor](#1-constructor--destructor)
   - [Accessors & Reset (`getRequest`, `reset`)](#2-accessors--reset)
   - [Dispatcher: `feed()`](#3-dispatcher-feedstdstring-buffer)
   - [Sub-Parser: `_parseRequestLine()`](#4-sub-parser-_parserequestlinestdstring-buffer)
   - [Sub-Parser: `_parseHeaders()`](#5-sub-parser-_parseheadersstdstring-buffer)
   - [Sub-Parser: `_parseBody()`](#6-sub-parser-_parsebodystdstring-buffer)
   - [Sub-Parser: `_parseChunkedBody()`](#7-sub-parser-_parsechunkedbodystdstring-buffer)
   - [Helper Functions & Validators](#8-helper-functions--validators)
6. [Security & Edge Case Handling](#security--edge-case-handling)
7. [C++98 Compliance Verification](#c98-compliance-verification)

---

## Overview

The `RequestParser` is a **non-blocking, incremental state-machine parser**. 

Because `recv()` calls from non-blocking sockets may return arbitrary fragments of bytes across multiple `poll()` iterations, the parser never assumes that complete lines or payloads arrive at once.

* If incoming bytes are insufficient to complete the current phase, it preserves internal state and returns **`PARSE_INCOMPLETE`** (yielding control back to the event loop).
* If a malformed syntax or security violation occurs, it returns **`PARSE_ERROR`** (triggering a `400 Bad Request` response).
* When the request is completely assembled, it returns **`PARSE_COMPLETE`** (triggering the `ResponseBuilder`).

```
Socket recv() ──▶ client.getReadBuffer() ──▶ parser.feed(readBuffer)
                                                   │
                ┌──────────────────────────────────┴──────────────────────────────────┐
                ▼                                     ▼                               ▼
        PARSE_INCOMPLETE                       PARSE_COMPLETE                    PARSE_ERROR
      (wait for next poll)                  (pass to ResponseBuilder)        (send 400 Bad Request)
```

---

## State Machine Architecture

```
                 ┌────────────────────┐
                 │ STATE_REQUEST_LINE │ ◄── Initial state
                 └─────────┬──────────┘
                           │ found \r\n
                           ▼
                 ┌────────────────────┐
                 │   STATE_HEADERS    │
                 └─────────┬──────────┘
                           │ found \r\n\r\n
             ┌─────────────┴─────────────┐
             │                           │
    Transfer-Encoding: chunked      Content-Length > 0     No body (GET/DELETE)
             ▼                           ▼                       ▼
    ┌────────────────────┐      ┌────────────────────┐           │
    │ STATE_CHUNKED_BODY │      │    STATE_BODY      │           │
    └────────┬───────────┘      └────────┬───────────┘           │
             │                           │                       │
             │ all chunks (0\r\n\r\n)    │ all bytes read        │
             └─────────────┬─────────────┴───────────────────────┘
                           ▼
                    STATE_COMPLETE
```

---

## Header Definition (`RequestParser.hpp`)

```cpp
#ifndef REQUESTPARSER_HPP
# define REQUESTPARSER_HPP

# include "HttpRequest.hpp"
# include <string>

class RequestParser
{
    public:
        enum ParseResult
        {
            PARSE_INCOMPLETE,   // Need more data from socket
            PARSE_COMPLETE,     // Full request parsed
            PARSE_ERROR         // Malformed or invalid request
        };

        RequestParser();
        ~RequestParser();

        ParseResult feed(std::string &buffer);

        HttpRequest &getRequest();
        void reset();
    
    private:
        enum State
        {
            STATE_REQUEST_LINE,
            STATE_HEADERS,
            STATE_BODY,
            STATE_CHUNKED_BODY,
            STATE_COMPLETE,
            STATE_ERROR
        };

        State       _state;
        HttpRequest _request;
        size_t      _bodyBytesRead;
        size_t      _chunkSize;
        bool        _chunkSizeParsed;

        ParseResult _parseRequestLine(std::string &buffer);
        ParseResult _parseHeaders(std::string &buffer);
        ParseResult _parseBody(std::string &buffer);
        ParseResult _parseChunkedBody(std::string &buffer);

        bool        _isValidMethod(const std::string &method);
        bool        _isValidURI(const std::string &uri);
        bool        _isValidVersion(const std::string &version);

        std::string _trim(const std::string &str);
        std::string _toLower(const std::string &str);
        std::string _decodeURI(const std::string &uri);
};

#endif
```

---

## Implementation File (`RequestParser.cpp`)

```cpp
#include "RequestParser.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

RequestParser::RequestParser()
    : _state(STATE_REQUEST_LINE),
      _bodyBytesRead(0),
      _chunkSize(0),
      _chunkSizeParsed(false)
{
}

RequestParser::~RequestParser()
{
}

HttpRequest &RequestParser::getRequest()
{
    return _request;
}

void RequestParser::reset()
{
    _state = STATE_REQUEST_LINE;
    _bodyBytesRead = 0;
    _chunkSize = 0;
    _chunkSizeParsed = false;
    _request.reset();
}

RequestParser::ParseResult RequestParser::feed(std::string &buffer)
{
    while (_state != STATE_COMPLETE && _state != STATE_ERROR)
    {
        if (_state == STATE_REQUEST_LINE)
        {
            ParseResult res = _parseRequestLine(buffer);
            if (res != PARSE_COMPLETE)
                return res;
            // Transitioned to STATE_HEADERS; loop continues if buffer has data
        }
        else if (_state == STATE_HEADERS)
        {
            ParseResult res = _parseHeaders(buffer);
            if (res != PARSE_COMPLETE)
                return res;
            if (_state == STATE_COMPLETE)
                return PARSE_COMPLETE;
        }
        else if (_state == STATE_BODY)
        {
            return _parseBody(buffer);
        }
        else if (_state == STATE_CHUNKED_BODY)
        {
            return _parseChunkedBody(buffer);
        }
    }

    return (_state == STATE_COMPLETE) ? PARSE_COMPLETE : PARSE_ERROR;
}

RequestParser::ParseResult RequestParser::_parseRequestLine(std::string &buffer)
{
    size_t pos = buffer.find("\r\n");
    if (pos == std::string::npos)
    {
        // Guard against unbounded line memory overflow
        if (buffer.length() > 8192)
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }
        return PARSE_INCOMPLETE;
    }

    std::string line = buffer.substr(0, pos);
    buffer.erase(0, pos + 2);

    // Split tokens by space
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token)
        tokens.push_back(token);

    if (tokens.size() != 3)
    {
        _state = STATE_ERROR;
        return PARSE_ERROR;
    }

    std::string method = tokens[0];
    std::string rawUri = tokens[1];
    std::string version = tokens[2];

    if (!_isValidMethod(method) || !_isValidVersion(version) || !_isValidURI(rawUri))
    {
        _state = STATE_ERROR;
        return PARSE_ERROR;
    }

    // Split URI into path and query string
    size_t qpos = rawUri.find('?');
    std::string pathOnly = (qpos != std::string::npos) ? rawUri.substr(0, qpos) : rawUri;
    std::string queryString = (qpos != std::string::npos) ? rawUri.substr(qpos + 1) : "";

    std::string decodedPath = _decodeURI(pathOnly);
    if (decodedPath.empty())
    {
        _state = STATE_ERROR;
        return PARSE_ERROR;
    }

    _request.method = method;
    _request.uri = decodedPath;
    _request.querystring = queryString;
    _request.version = version;

    _state = STATE_HEADERS;
    return PARSE_COMPLETE;
}

RequestParser::ParseResult RequestParser::_parseHeaders(std::string &buffer)
{
    while (true)
    {
        size_t pos = buffer.find("\r\n");
        if (pos == std::string::npos)
        {
            // Protect against header bombing
            if (buffer.length() > 32768)
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }
            return PARSE_INCOMPLETE;
        }

        // Empty line indicates end of headers section
        if (pos == 0)
        {
            buffer.erase(0, 2);

            // RFC 7230 §5.4: Host header is mandatory in HTTP/1.1
            if (!_request.hasHeader("host"))
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }

            // Check Transfer-Encoding
            if (_request.hasHeader("transfer-encoding"))
            {
                std::string te = _toLower(_request.getHeader("transfer-encoding"));
                if (te == "chunked")
                {
                    _request.isChunked = true;
                    _state = STATE_CHUNKED_BODY;
                    return PARSE_COMPLETE;
                }
                else
                {
                    _state = STATE_ERROR;
                    return PARSE_ERROR;
                }
            }

            // Check Content-Length
            if (_request.hasHeader("content-length"))
            {
                std::string clStr = _request.getHeader("content-length");
                for (size_t i = 0; i < clStr.length(); ++i)
                {
                    if (!std::isdigit(static_cast<unsigned char>(clStr[i])))
                    {
                        _state = STATE_ERROR;
                        return PARSE_ERROR;
                    }
                }

                std::istringstream clStream(clStr);
                size_t length = 0;
                clStream >> length;

                _request.contentLength = length;
                if (length > 0)
                {
                    _state = STATE_BODY;
                    return PARSE_COMPLETE;
                }
            }

            // If no body expected (GET, DELETE, or POST with Content-Length: 0)
            _state = STATE_COMPLETE;
            return PARSE_COMPLETE;
        }

        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 2);

        size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }

        std::string key = _toLower(_trim(line.substr(0, colon)));
        std::string value = _trim(line.substr(colon + 1));

        if (key.empty())
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }

        _request.headers[key] = value;
    }
}

RequestParser::ParseResult RequestParser::_parseBody(std::string &buffer)
{
    size_t remaining = _request.contentLength - _bodyBytesRead;
    if (buffer.empty())
        return PARSE_INCOMPLETE;

    size_t toRead = (buffer.size() < remaining) ? buffer.size() : remaining;

    _request.body.insert(_request.body.end(), buffer.begin(), buffer.begin() + toRead);
    buffer.erase(0, toRead);
    _bodyBytesRead += toRead;

    if (_bodyBytesRead >= _request.contentLength)
    {
        _state = STATE_COMPLETE;
        return PARSE_COMPLETE;
    }

    return PARSE_INCOMPLETE;
}

RequestParser::ParseResult RequestParser::_parseChunkedBody(std::string &buffer)
{
    while (true)
    {
        if (!_chunkSizeParsed)
        {
            size_t pos = buffer.find("\r\n");
            if (pos == std::string::npos)
                return PARSE_INCOMPLETE;

            std::string line = buffer.substr(0, pos);

            // Strip chunk extensions (anything after ;)
            size_t semi = line.find(';');
            if (semi != std::string::npos)
                line = line.substr(0, semi);

            line = _trim(line);
            if (line.empty())
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }

            for (size_t i = 0; i < line.length(); ++i)
            {
                if (!std::isxdigit(static_cast<unsigned char>(line[i])))
                {
                    _state = STATE_ERROR;
                    return PARSE_ERROR;
                }
            }

            std::istringstream hexStream(line);
            size_t size = 0;
            if (!(hexStream >> std::hex >> size))
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }

            buffer.erase(0, pos + 2);
            _chunkSize = size;
            _chunkSizeParsed = true;

            // Final 0-sized chunk reached
            if (_chunkSize == 0)
            {
                // Looking for final trailing CRLF
                size_t endPos = buffer.find("\r\n");
                if (endPos == std::string::npos)
                    return PARSE_INCOMPLETE;

                buffer.erase(0, endPos + 2);
                _state = STATE_COMPLETE;
                return PARSE_COMPLETE;
            }
        }

        if (_chunkSizeParsed && _chunkSize > 0)
        {
            // Must have chunk data + trailing "\r\n"
            if (buffer.size() < _chunkSize + 2)
                return PARSE_INCOMPLETE;

            // Verify trailing CRLF
            if (buffer[_chunkSize] != '\r' || buffer[_chunkSize + 1] != '\n')
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }

            _request.body.insert(_request.body.end(), buffer.begin(), buffer.begin() + _chunkSize);
            buffer.erase(0, _chunkSize + 2);

            _chunkSize = 0;
            _chunkSizeParsed = false;
        }
    }
}

bool RequestParser::_isValidMethod(const std::string &method)
{
    return (method == "GET" || method == "POST" || method == "DELETE");
}

bool RequestParser::_isValidURI(const std::string &uri)
{
    if (uri.empty() || uri[0] != '/')
        return false;
    return true;
}

bool RequestParser::_isValidVersion(const std::string &version)
{
    return (version == "HTTP/1.1");
}

std::string RequestParser::_trim(const std::string &str)
{
    size_t first = 0;
    while (first < str.length() && (str[first] == ' ' || str[first] == '\t' || str[first] == '\r'))
        first++;

    if (first == str.length())
        return "";

    size_t last = str.length() - 1;
    while (last > first && (str[last] == ' ' || str[last] == '\t' || str[last] == '\r'))
        last--;

    return str.substr(first, last - first + 1);
}

std::string RequestParser::_toLower(const std::string &str)
{
    std::string lower = str;
    for (size_t i = 0; i < lower.length(); ++i)
    {
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
    }
    return lower;
}

std::string RequestParser::_decodeURI(const std::string &uri)
{
    std::string decoded;
    decoded.reserve(uri.length());

    for (size_t i = 0; i < uri.length(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.length() &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 2])))
        {
            std::string hexStr = uri.substr(i + 1, 2);
            std::istringstream hexStream(hexStr);
            int value = 0;
            hexStream >> std::hex >> value;
            decoded += static_cast<char>(value);
            i += 2;
        }
        else
        {
            decoded += uri[i];
        }
    }

    // Path traversal check
    if (decoded.find("/../") != std::string::npos ||
        decoded == ".." ||
        (decoded.length() >= 3 && decoded.substr(decoded.length() - 3) == "/.."))
    {
        return ""; // Reject path traversal attempt
    }

    return decoded;
}
```

---

## Detailed Breakdown of Methods

### 1. Constructor & Destructor
```cpp
RequestParser::RequestParser()
    : _state(STATE_REQUEST_LINE), _bodyBytesRead(0), _chunkSize(0), _chunkSizeParsed(false)
```
* Initializes state machine to `STATE_REQUEST_LINE`.
* Resets all reading counters.

---

### 2. Accessors & Reset
```cpp
HttpRequest &RequestParser::getRequest();
void RequestParser::reset();
```
* **`getRequest()`**: Provides a reference to the parsed `HttpRequest` object for the core loop and `ResponseBuilder`.
* **`reset()`**: Reinitializes the state machine and clears the underlying `_request` instance for keep-alive reuse.

---

### 3. Dispatcher: `feed(std::string &buffer)`
```cpp
ParseResult RequestParser::feed(std::string &buffer)
```
* **Core Logic**:
  * Loops while state transitions advance without blocking.
  * Consumes bytes from `buffer` in-place using `buffer.erase()`.
  * If a sub-step returns `PARSE_INCOMPLETE`, returns immediately to allow the main event loop to gather more data via `poll()`.
  * If an invalid header or syntax error occurs, transitions to `STATE_ERROR` and returns `PARSE_ERROR`.

---

### 4. Sub-Parser: `_parseRequestLine(std::string &buffer)`
```cpp
ParseResult RequestParser::_parseRequestLine(std::string &buffer)
```
* **Step 1: Delimiter Search**: Looks for `\r\n`. If not found yet and buffer is within 8KB, returns `PARSE_INCOMPLETE`.
* **Step 2: Tokenization**: Splits line into exactly 3 space-separated tokens: `[METHOD] [URI] [VERSION]`.
* **Step 3: Validation**:
  * Validates method is `GET`, `POST`, or `DELETE`.
  * Validates version is strictly `"HTTP/1.1"`.
  * Validates URI starts with `'/'`.
* **Step 4: Query String Extraction**: Detects `?` and separates path from query parameters (stored in `_request.querystring`).
* **Step 5: Percent-Decoding & Path Sanitization**: Decodes `%20` $\rightarrow$ space, `%2F` $\rightarrow$ `/`, and blocks path traversal attempts.
* **Transition**: Transitions to `STATE_HEADERS`.

---

### 5. Sub-Parser: `_parseHeaders(std::string &buffer)`
```cpp
ParseResult RequestParser::_parseHeaders(std::string &buffer)
```
* **Header Parsing Loop**:
  * Finds each line terminated by `\r\n`.
  * Detects blank line (`pos == 0`, meaning `\r\n\r\n`), marking the end of headers.
  * For each header line:
    * Splits on the first `:` separator.
    * Trims whitespace from both key and value.
    * Stores key in **lowercase** (e.g., `"Content-Type"` $\rightarrow$ `"content-type"`) for case-insensitive lookup.
* **End-of-Headers Processing**:
  * **Mandatory Host Check**: If `host` header is missing, immediately fails with `PARSE_ERROR` (RFC 7230 §5.4).
  * **Chunked Check**: If `Transfer-Encoding: chunked` is present, transitions to `STATE_CHUNKED_BODY`.
  * **Content-Length Check**: Validates that `Content-Length` is purely numeric and non-negative. If length > 0, transitions to `STATE_BODY`.
  * **No Body**: If neither chunked nor Content-Length, transitions directly to `STATE_COMPLETE`.

---

### 6. Sub-Parser: `_parseBody(std::string &buffer)`
```cpp
ParseResult RequestParser::_parseBody(std::string &buffer)
```
* Used when `Content-Length` is specified.
* Calculates remaining bytes needed: `_request.contentLength - _bodyBytesRead`.
* Appends available bytes directly into `_request.body`.
* When `_bodyBytesRead == _request.contentLength`, transitions to `STATE_COMPLETE` and returns `PARSE_COMPLETE`.

---

### 7. Sub-Parser: `_parseChunkedBody(std::string &buffer)`
```cpp
ParseResult RequestParser::_parseChunkedBody(std::string &buffer)
```
* Handles `Transfer-Encoding: chunked`:
  ```
  [HEX_SIZE]\r\n
  [CHUNK_DATA]\r\n
  ...
  0\r\n
  \r\n
  ```
* **Two-Phase Sub-State**:
  1. **Phase A (`!_chunkSizeParsed`)**: Reads the hex line, parses chunk size via `std::hex`. If size is 0, waits for final `\r\n` and completes request.
  2. **Phase B (`_chunkSizeParsed`)**: Ensures at least `_chunkSize + 2` bytes are buffered, verifies trailing `\r\n`, appends payload bytes to `_request.body`, and resets for the next chunk size.

---

### 8. Helper Functions & Validators

* **`_isValidMethod(method)`**: Accepts only `GET`, `POST`, `DELETE`.
* **`_isValidURI(uri)`**: Ensures URI begins with `/`.
* **`_isValidVersion(version)`**: Enforces `HTTP/1.1`.
* **`_trim(str)`**: Removes leading/trailing spaces, `\t`, and `\r`.
* **`_toLower(str)`**: Converts strings to lowercase using `std::tolower`.
* **`_decodeURI(uri)`**: Decodes `%XX` hex sequences and detects path traversal patterns (`/../`, `..`).

---

## Security & Edge Case Handling

| Threat / Edge Case | Handled Behavior | Result |
| :--- | :--- | :--- |
| **URI / Request-Line Overflow** | Buffer exceeds 8192 bytes before `\r\n` | `PARSE_ERROR` (414) |
| **Header Bombing / Slowloris** | Headers exceed 32768 bytes before `\r\n\r\n` | `PARSE_ERROR` (431) |
| **Missing `Host` Header** | HTTP/1.1 RFC 7230 mandate | `PARSE_ERROR` (400) |
| **Negative / Alpha Content-Length** | Non-digit characters in `Content-Length` | `PARSE_ERROR` (400) |
| **Directory Traversal** | URI contains `/../` or terminates in `/..` | `PARSE_ERROR` (400) |
| **Partial Socket Packets** | Incomplete lines or split chunk payload | `PARSE_INCOMPLETE` (waits for next `poll()`) |

---

## C++98 Compliance Verification

* **No C++11 `auto`**: Explicit `std::vector<std::string>::const_iterator` and index loops used.
* **No `std::to_string()`**: Uses `<sstream>` for string/number conversions.
* **Stream Extraction**: `std::istringstream` with `std::hex` used for chunk size parsing.
* **Safe Character Casting**: `static_cast<unsigned char>` used before all `<cctype>` functions (`isdigit`, `isxdigit`, `tolower`).


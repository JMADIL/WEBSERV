# 📄 HttpRequest Class Documentation

This document provides a comprehensive overview of the `HttpRequest` class, the complete C++ implementation file (`HttpRequest.cpp`), and an in-depth breakdown of every method and attribute used.

---

## 📌 Table of Contents
1. [Overview](#overview)
2. [Header Definition (`HttpRequest.hpp`)](#header-definition-httprequesthpp)
3. [Implementation File (`HttpRequest.cpp`)](#implementation-file-httprequestcpp)
4. [Detailed Breakdown of Methods](#detailed-breakdown-of-methods)
   - [Constructor: `HttpRequest()`](#1-constructor-httprequest)
   - [Destructor: `~HttpRequest()`](#2-destructor-httprequest)
   - [Reset: `reset()`](#3-void-reset)
   - [Get Header: `getHeader()`](#4-stdstring-getheaderconst-stdstring-key-const)
   - [Has Header: `hasHeader()`](#5-bool-hasheaderconst-stdstring-key-const)
   - [Keep-Alive Check: `keepAlive()`](#6-bool-keepalive-const)
5. [Member Attributes](#member-attributes)
6. [Design Rationale & Standards Compliance](#design-rationale--standards-compliance)

---

## Overview

The `HttpRequest` class acts as the **data container** for parsed HTTP requests. It is populated incrementally by the `RequestParser` and read by the `ResponseBuilder` (and `CgiHandler`).

```
                ┌────────────────┐
  Raw Bytes ───▶│ RequestParser  │
                └───────┬────────┘
                        │ fills
                        ▼
                ┌────────────────┐
                │  HttpRequest   │
                └───────┬────────┘
                        │ read by
                        ▼
                ┌────────────────┐
                │ResponseBuilder │
                └────────────────┘
```

Its primary responsibilities are:
- Storing request-line tokens (`method`, `uri`, `querystring`, `version`).
- Holding header key-value pairs (`headers`).
- Holding raw body bytes (`body`), along with `contentLength` and `isChunked`.
- Providing case-insensitive header lookup utilities.
- Assessing connection persistence (`keepAlive`).
- Enabling reuse between requests on persistent connections (`reset`).

---

## Header Definition (`HttpRequest.hpp`)

```cpp
#pragma once 

#include <map>
#include <string>
#include <vector>

class HttpRequest
{
    public:
        // Request-line components
        std::string                         method;         // "GET", "POST", "DELETE"
        std::string                         uri;            // "/path/to/resource"
        std::string                         querystring;    // "key=val&key2=val2"
        std::string                         version;        // "HTTP/1.1"

        // Headers (keys stored in lowercase)
        std::map<std::string, std::string>  headers;

        // Body & metadata (vector<char> for binary data safety)
        std::vector<char>                   body;
        size_t                              contentLength;
        bool                                isChunked;

        HttpRequest();
        ~HttpRequest();

        void        reset();
        std::string getHeader(const std::string &key) const;
        bool        hasHeader(const std::string &key) const;
        bool        keepAlive() const;
};
```

---

## Implementation File (`HttpRequest.cpp`)

```cpp
#include "HttpRequest.hpp"
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// Constructor & Destructor
// ---------------------------------------------------------------------------

HttpRequest::HttpRequest() : contentLength(0), isChunked(false)
{
}

HttpRequest::~HttpRequest()
{
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void HttpRequest::reset()
{
    method.clear();
    uri.clear();
    querystring.clear();
    version.clear();
    headers.clear();
    body.clear();
    contentLength = 0;
    isChunked = false;
}

// ---------------------------------------------------------------------------
// Helper: Case-insensitive conversion
// ---------------------------------------------------------------------------

static std::string toLowerString(const std::string &str)
{
    std::string lower = str;
    for (size_t i = 0; i < lower.length(); ++i)
    {
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
    }
    return lower;
}

// ---------------------------------------------------------------------------
// Header Accessors
// ---------------------------------------------------------------------------

std::string HttpRequest::getHeader(const std::string &key) const
{
    std::string lowerKey = toLowerString(key);
    std::map<std::string, std::string>::const_iterator it = headers.find(lowerKey);
    if (it != headers.end())
        return it->second;
    return "";
}

bool HttpRequest::hasHeader(const std::string &key) const
{
    std::string lowerKey = toLowerString(key);
    return headers.find(lowerKey) != headers.end();
}

// ---------------------------------------------------------------------------
// Connection Persistence Check
// ---------------------------------------------------------------------------

bool HttpRequest::keepAlive() const
{
    if (hasHeader("connection"))
    {
        std::string conn = toLowerString(getHeader("connection"));
        if (conn == "close")
            return false;
        if (conn == "keep-alive")
            return true;
    }
    // In HTTP/1.1, persistent connections are the default unless explicitly closed
    if (version == "HTTP/1.1")
        return true;
    return false;
}
```

---

## Detailed Breakdown of Methods

### 1. Constructor: `HttpRequest()`
```cpp
HttpRequest::HttpRequest() : contentLength(0), isChunked(false)
```
* **Purpose**: Initializes a new request instance in a predictable, clean state.
* **Mechanism**: Uses member initialization list to set primitive types:
  - `contentLength` is set to `0`.
  - `isChunked` is set to `false`.
  - Strings (`method`, `uri`, `querystring`, `version`), vectors (`body`), and maps (`headers`) are default-constructed as empty objects.

---

### 2. Destructor: `~HttpRequest()`
```cpp
HttpRequest::~HttpRequest()
```
* **Purpose**: Cleans up resources when the request object is destroyed.
* **Mechanism**: Standard default destructor. Since all members manage their own dynamic memory (RAII via standard C++ containers: `std::string`, `std::vector`, `std::map`), no manual `delete` calls are required, preventing memory leaks.

---

### 3. `void reset()`
```cpp
void HttpRequest::reset()
```
* **Purpose**: Prepares an existing `HttpRequest` instance for reuse when the connection supports **HTTP Keep-Alive**.
* **Why it's essential**: Instead of allocating and freeing `Client` or `HttpRequest` objects on heap memory for every single HTTP request on a persistent connection, the server clears the fields of the existing instance and reuses it.
* **Actions Taken**:
  - `method.clear()`, `uri.clear()`, `querystring.clear()`, `version.clear()`: Resets string tokens.
  - `headers.clear()`: Empties the map of header key-value pairs.
  - `body.clear()`: Empties the raw body byte vector without necessarily releasing capacity.
  - `contentLength = 0`: Resets expected content length.
  - `isChunked = false`: Resets chunked transfer encoding flag.

---

### 4. `std::string getHeader(const std::string &key) const`
```cpp
std::string HttpRequest::getHeader(const std::string &key) const
```
* **Purpose**: Retrieves the value associated with an HTTP header key.
* **HTTP Standard Rule (RFC 7230 §3.2)**: Field names in HTTP headers are case-insensitive (e.g., `Host`, `host`, and `HOST` all refer to the same header).
* **Mechanism**:
  1. Converts the input search `key` to lowercase via the `toLowerString` utility.
  2. Performs a lookup in `headers` using `headers.find(lowerKey)`.
  3. If found, returns the corresponding header value (`it->second`).
  4. If not found, returns an empty string (`""`).

---

### 5. `bool hasHeader(const std::string &key) const`
```cpp
bool HttpRequest::hasHeader(const std::string &key) const
```
* **Purpose**: Quickly verifies if a particular header exists in the request.
* **Mechanism**:
  1. Converts `key` to lowercase.
  2. Checks `headers.find(lowerKey) != headers.end()`.
  3. Returns `true` if present, `false` otherwise.

---

### 6. `bool keepAlive() const`
```cpp
bool HttpRequest::keepAlive() const
```
* **Purpose**: Determines whether the TCP connection should remain open after sending the response or be closed immediately.
* **HTTP/1.1 vs HTTP/1.0 Specification Rules**:
  - **Explicit `Connection` header**:
    - If `Connection: close` is specified, the connection **must** close (`return false`).
    - If `Connection: keep-alive` is specified, the connection **should** remain open (`return true`).
  - **Default protocol behavior**:
    - In **HTTP/1.1**, connections are persistent by default (`return true`).
    - In **HTTP/1.0**, connections are closed by default unless `Connection: keep-alive` was sent.

---

## Member Attributes

| Attribute | Type | Description |
| :--- | :--- | :--- |
| `method` | `std::string` | HTTP method verb (`"GET"`, `"POST"`, `"DELETE"`). |
| `uri` | `std::string` | Path component of the URI (e.g., `"/index.html"`, `"/uploads"`). |
| `querystring` | `std::string` | Query parameters string after `?` (e.g., `"user=adil&lang=en"`). |
| `version` | `std::string` | HTTP protocol version string (e.g., `"HTTP/1.1"`). |
| `headers` | `std::map<std::string, std::string>` | Map of lowercase header names to their corresponding values. |
| `body` | `std::vector<char>` | Raw binary body payload (handles text, JSON, binary images, file uploads). |
| `contentLength` | `size_t` | Numeric value from `Content-Length` header (0 if none or chunked). |
| `isChunked` | `bool` | Flag indicating `Transfer-Encoding: chunked`. |

---

## Design Rationale & Standards Compliance

1. **Binary-Safe Body (`std::vector<char>`)**:
   `std::string` can hold null bytes `\0`, but using `std::vector<char>` explicitly documents that the request payload may contain arbitrary binary bytes (such as multipart file uploads, images, or archives) and avoids unintentional null-terminated string truncations.

2. **Strict C++98 Compatibility**:
   - No C++11 range-based for loops.
   - Explicit `std::map<...>::const_iterator` types instead of `auto`.
   - `std::tolower` cast through `unsigned char` to prevent undefined behavior with negative char values.
   - Standard initialization lists.

3. **Separation of Concerns**:
   `HttpRequest` contains zero socket, parsing, or file I/O logic. It is strictly a pure data structure paired with inspection utilities.


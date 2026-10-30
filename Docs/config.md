# Configuration loading

The configuration module is implemented in C++98 without external libraries.
It reads configuration before listener startup. It does not open sockets or
implement HTTP/CGI behavior.

## Public contract

```cpp
#include "Config.hpp"

Config config;
config.load("configs/default.conf");
const std::vector<ServerBlock>& servers = config.getServers();
```

`load()` throws `std::runtime_error` with `filename:line: explanation` for
configuration errors. The caller must catch errors and stop startup. Standard
allocation exceptions can also propagate. Failed loads preserve the previous
configuration. Successful loads replace it rather than append sites.

Keep the Config object alive while the server uses references to its settings.
Do not reload it while clients hold references or location pointers; safe live
reload is outside this implementation's scope.

## How the implementation works

1. `tokenize()` reads the file and creates internal Token objects, each containing
   its text and starting line. Braces and semicolons are individual tokens even
   without surrounding spaces. An empty token at the end marks EOF.
2. The internal Parser consumes tokens in order. It expects server blocks at
   the top level and location blocks inside servers. Directive arguments must
   end in a semicolon; a newline alone does not end a directive.
3. Directive handlers validate context, argument counts, duplicates, and values
   before calling ServerBlock/LocationBlock setters.
4. Each completed server receives `applyLocationDefaults()`. A missing location
   body limit inherits the server limit, regardless of directive order. An
   explicit zero-byte limit remains zero.
5. Only after the whole file passes does `Config::load()` swap the new server
   collection into place.

## Supported grammar

```text
config   := server+
server   := server { (server-directive | location)* }
location := location /path { location-directive* }
```

This is a deliberately small NGINX-inspired format, not an NGINX configuration
implementation. Values are unquoted whitespace-separated words. `#` begins a
comment through the end of the line. Blank lines, tabs, CRLF, compact braces,
and multiline directives are accepted. Quotes and escapes are rejected.
Literal whitespace, braces, semicolons, and # cannot appear inside a value.
Files are limited to 1 MiB, including comments, to bound parser input storage.
Other control characters, including NUL, are rejected.

| Directive | Context | Accepted values |
| --- | --- | --- |
| `listen 8080;` | server | Decimal port 1–65535; host defaults to 0.0.0.0 |
| `listen 127.0.0.1:8080;` | server | Four decimal IPv4 octets, each 0–255; normalized before checking duplicates |
| `server_name localhost;` | server | One unquoted name; stored as metadata, without implementing virtual hosts |
| `client_max_body_size 10M;` | server or location | Nonnegative decimal bytes or uppercase K/M suffix; overflow rejected |
| `error_page 404 /error_pages/404.html;` | server | One status 400–599 and a normalized site-relative URL path |
| `root ./www;` | location | Nonempty filesystem path |
| `index index.html;` | location | One filename; no slash, `.` or `..` |
| `allowed_methods GET POST DELETE;` | location | One or more distinct GET/POST/DELETE values |
| `autoindex on;` | location | Exactly on or off |
| `redirect 301 /new;` | location | Exactly 301 or 302 plus one destination |
| `upload_path ./www/uploads;` | location | Nonempty filesystem path |
| `cgi_extension .py /usr/bin/python3;` | location | Dot plus letters/digits/underscore/hyphen; absolute interpreter path |

K is 1024 and M is 1048576. A configured zero body limit permits no body bytes.
Each server requires exactly one listen and at least one location. Each
file-serving location requires root; redirect-only locations do not.

Duplicate singleton directives, location paths, error status mappings, CGI
extensions, and methods are rejected. Error pages may repeat for different
status codes; CGI mappings may repeat for different extensions. Unknown or
misplaced directives, missing delimiters, and trailing content are rejected.

Location paths start with `/`, have no query, percent escapes, repeated slash,
or `.`/`..` segment. Paths ending in `/` are supported. Regex, nested locations,
includes, IPv6, hostname resolution in listen, and name-based virtual hosts are
outside this grammar. Add separate server blocks for multiple endpoints.

Servers cannot claim the same normalized address and port. A wildcard listener
0.0.0.0 also conflicts with another configured address on the same port.
Different explicit interfaces on the same port are accepted; actual availability
and binding errors must be checked by the future core.

## Defaults and filesystem responsibilities

Server body limit: 1 MiB. Location defaults are inherited body limit, GET,
index.html, autoindex off, no redirect, no CGI mapping, and no upload storage.

Relative filesystem paths are relative to the program's launch directory.
Custom error-page paths are URL paths, to be resolved under the selected site's
`/` location root by the response code. If there is no `/` route, or its error
file cannot be served, the handler should use a generated default error page.

The parser does not check whether content directories, files, upload folders,
or interpreters exist or are accessible. The relevant runtime handlers must
check this and produce appropriate errors. It does not create those resources.
For our path mapping, remove the selected route prefix before joining the
remaining request path to its configured root. The HTTP code must normalize
and safely constrain request paths; storing a config root does not provide
path traversal protection on its own.

`configs/default.conf` demonstrates all supported feature categories.
`configs/test.conf` demonstrates two ports and distinct document roots. These
are parser examples; the website content, upload folders, CGI script, and
second site's files must still be supplied for a full-server demonstration.

## Reproducible checks

From the repository root:

```bash
python3 tests/run_config_tests.py
```

Requires Python 3 and a C++ compiler. The runner compiles only the configuration
module with `-Wall -Wextra -Werror -std=c++98`, runs assertions, and deletes its
temporary executable and fixtures. Set CXX to a compiler executable if needed.

Tests cover both example files, resulting settings, defaults, inheritance,
multiple sites, reload replacement, preserved configuration after failed
loads, line diagnostics, IPv4/port validation, syntax errors, duplicate
directives, numeric boundaries and overflow, the input-size limit, NUL input,
and every incomplete prefix of a valid configuration. These checks do not
prove HTTP integration or implement the project's final executable/Makefile.

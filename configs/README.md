# Running the current configurations

Launch from the repository root. Paths are relative to the working directory;
the configs do not create directories. The repository supplies `www`,
`www/uploads`, `www-second` and the Python demo under `cgi-bin`.
The configured interpreter is `/usr/bin/python3`.

```sh
make
./webserv configs/default.conf
```

| Endpoint | Configured methods | Incoming body limit | Current purpose |
| --- | --- | --- | --- |
| `127.0.0.1:8080/` | GET | 0 bytes | Static website from `./www`; listing off. |
| `127.0.0.1:8080/uploads` | GET, POST, DELETE | 10 MiB | Store files in `./www/uploads`; listing on. |
| `127.0.0.1:8080/cgi-bin` | GET, POST | 1 MiB | Run `.py` files with Python; directory index is `test.py`. |
| `127.0.0.1:8080/redirect` | GET | 0 bytes | Configured 301 redirect to `/`. |

The first site's default body limit is 10 MiB; explicit route settings document each
route's policy. Zero means no request body, not unlimited. These are request
limits, not static-response or CGI-output limits. Core's CGI output cap remains
8 MiB, and its CGI timeout remains 15 seconds; neither has a config directive.

`test.conf` runs the same first website plus a static-only second website at
`127.0.0.1:8081`, rooted at `./www-second`, with no request body allowed. Stop the
default instance before starting it because both configurations use port 8080.
`server_name` is metadata, not name-based virtual-host routing.

```sh
./webserv configs/test.conf
```

The first site maps the supplied 400/403/404/405/413/500 error files. Other errors
use generated fallbacks; the second site uses generated errors. Existing HTTP
handler defects remain tracked in `../Docs/remaining_problems.md`.

## Examples

```sh
curl http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/missing
curl -i http://127.0.0.1:8080/redirect
curl --data-binary 'example' http://127.0.0.1:8080/uploads/example.txt
curl http://127.0.0.1:8080/uploads/example.txt
curl -X DELETE http://127.0.0.1:8080/uploads/example.txt
curl 'http://127.0.0.1:8080/cgi-bin/?name=example'
curl --data-binary 'hello CGI' http://127.0.0.1:8080/cgi-bin/test.py/extra
```

## Cookies and sessions: current boundary

There is no `session`, `sessions`, `cookie`, `session_timeout` or similar
directive in the current parser. Adding one would stop startup with an unknown
directive error. Cookies travel through HTTP Cookie/Set-Cookie headers rather
than requiring a new location setting. CGI receives request cookies through
`HTTP_COOKIE` and can emit Set-Cookie fields.

The C++ SessionManager compiles separately, but the current HTTP handlers never
create/look up a managed session, and session sources are not in the mandatory
executable. A config change cannot activate that missing request flow. Its
constructor's 1800-second default is not a configurable server setting.

Adil must wire session lookup/creation, response cookies, expiry and an example
route before a server-session demonstration can be claimed. Once those routes
and any desired configuration settings are agreed, extend the configs to match
the implemented interface. These configs do not pretend that bonus is enabled.

## Checks

```sh
python3 tests/run_config_tests.py
python3 tests/test_examples.py
python3 tests/test_startup.py
```

The example tests use available temporary ports and verify both configs, route
limits, static content, uploads/deletion, redirects and actual Python CGI.

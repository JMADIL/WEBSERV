# Merged main — import and full-project test report

What this repo does: Webserv is a C++98 HTTP server with a shared poll loop,
static-file/upload handlers, CGI and a login/logout session demonstration.
This review assumes concurrent local clients, fragmented requests, slow or
disconnecting clients, binary uploads and potentially large static files.

Reviewed on 2026-11-01. **The imported version builds, but is not finished.**
The five supplied smoke tests pass; broader acceptance testing reproduces
19 failures/errors across 42 tests. Some checks concern optional HTTP/bonus
behavior, but the blocking FIFO and file-handling problems are serious failures
independently of those optional features.

## Exactly what was imported

- Source: `/home/ily4s/Desktop/adil_part`, branch `main`.
- Commit: `e3b792c2eb393d2da6f3dc5ac8d1157a5563d017` (`probably the last commit`).
- Imported 64 tracked implementation/config/example/test files unchanged.
- Preserved local notes, README, older tests and extra demo content. Existing
  tracked Docs were kept rather than overwriting local edits with older notes.
- Removed the two obsolete RequestParserAdapter source/header files because the
  merged branch uses RequestParser directly. The backup contains the old files.
- Kept the imported Makefile and implementation unchanged after import. Only
  local test harnesses/documentation were added or adapted for the new API.
- Backup: `/home/ily4s/Desktop/WEBSERV-backup-before-e3b792c-20261101-154952`.
- File hashes and copied-file list: [import manifest](/home/ily4s/Desktop/WEBSERV/Docs/test_results/import_manifest.json).

The external Adil checkout was not edited. This report describes its actual
imported code, rather than an unreported repaired version.

## Test results

| Check | Result | Evidence |
| --- | --- | --- |
| Clean rebuild and supplied `make test` | Pass; all 5 supplied tests | [build](/home/ily4s/Desktop/WEBSERV/Docs/test_results/build.log), [baseline](/home/ily4s/Desktop/WEBSERV/Docs/test_results/baseline.log) |
| New independent socket acceptance suite | 42 tests: 22 pass, 16 assertions fail, 3 errors, 1 skip | [acceptance](/home/ily4s/Desktop/WEBSERV/Docs/test_results/acceptance.log) |
| Clang AddressSanitizer + UndefinedBehaviorSanitizer + leak detection | Same 42-test results; no sanitizer diagnostics detected in these exercised paths | [sanitizers](/home/ily4s/Desktop/WEBSERV/Docs/test_results/sanitizers.log) |
| Chromium browser | Home, login, HttpOnly cookie creation, logout, JavaScript binary upload/download/delete pass; no page errors | [browser log](/home/ily4s/Desktop/WEBSERV/Docs/test_results/browser.log), [login screenshot](/home/ily4s/Desktop/WEBSERV/Docs/test_results/browser/login.png), [upload screenshot](/home/ily4s/Desktop/WEBSERV/Docs/test_results/browser/upload.png) |
| Listener/Client ownership | Pass | [Core](/home/ily4s/Desktop/WEBSERV/Docs/test_results/core.log) |
| Startup/build rules | Pass: arguments, invalid inputs, dependencies, no relink, clean/fclean/re and parallel builds | [startup](/home/ily4s/Desktop/WEBSERV/Docs/test_results/startup.log) |
| Core resource fixtures | Pass: 9 MiB streamed transport, truncation, limits, RSS plateau, descriptor exhaustion/recovery and injected allocation failure | [resources](/home/ily4s/Desktop/WEBSERV/Docs/test_results/resources.log) |
| CGI fixtures | Routing passes; 22 launcher, 36 response and 17 lifecycle modes pass | [routing](/home/ily4s/Desktop/WEBSERV/Docs/test_results/routing.log), [launch](/home/ily4s/Desktop/WEBSERV/Docs/test_results/launch.log), [response](/home/ily4s/Desktop/WEBSERV/Docs/test_results/response.log), [lifecycle](/home/ily4s/Desktop/WEBSERV/Docs/test_results/lifecycle.log) |
| Config/HTTP site context | Pass, including same-port distinct interfaces, custom errors and CGI local redispatch | [compatibility](/home/ily4s/Desktop/WEBSERV/Docs/test_results/config_compatibility.log) |
| Current shipped examples | Pass: both configs, upload/GET/DELETE, body limits and Python demo | [examples](/home/ily4s/Desktop/WEBSERV/Docs/test_results/examples.log) |
| Historical strict-config regression cases | 115 negative cases rejected; 6 accepted despite earlier grammar expectations | [configuration](/home/ily4s/Desktop/WEBSERV/Docs/test_results/config_regressions.log) |
| Parser boundary regression | Fails trailer/leftover-buffer expectations | [parser](/home/ily4s/Desktop/WEBSERV/Docs/test_results/parser_regressions.log) |
| Cookie/session class probe | Stored lookup/expiry pass; repeated cookies and deterministic-ID checks fail | [session probe](/home/ily4s/Desktop/WEBSERV/Docs/test_results/session_probe.log) |
| Previous targeted HTTP checks | 7 still fail; split zero chunk, 405 Allow and session compilation pass | [previous checks](/home/ily4s/Desktop/WEBSERV/Docs/test_results/known_checks.log) |

The socket acceptance suite starts an independent server with disposable files
for each test. A stalled FIFO therefore cannot hide later results. It uses the
actual executable, HTTP parser/builder, Core and CGI, without replacing them.
It also tests 160 concurrent-workload requests, partial input, idle timeout,
CGI admission at 16 jobs, a 1 MiB binary CGI body, cancellation and reaping.

The dedicated launch/lifecycle fixtures intentionally isolate HTTP entry to test
ownership and failure paths. Their success does not prove the production file
handlers or every HTTP boundary is correct. Sanitizer success likewise does not
resolve functional or protocol failures.

## Must fix

### 1. [P1] A FIFO request stops the server from serving other work

- **What this is:** Static/error content passes through
  [ResponseBuilder::_readFile](/home/ily4s/Desktop/WEBSERV/srcs/http/ResponseBuilder.cpp:440).
- **Problem:** GET on a FIFO waits in the ordinary file reader; the request times
  out and the blocked test instance requires forced termination. Core's timers
  cannot run while the HTTP handler is blocked there.
- **Fix:** Open safely without blocking, verify regular files and refuse special
  files before normal file reads. Apply the same policy to uploads and errors;
  only regular disk files receive the subject's poll exemption.
- **If skipped:** One such request can stall the single server thread, conflicting
  directly with the subject's nonblocking/availability requirement.

### 2. [P1] HTTP file paths can escape their configured root

- **What this is:** GET and POST use filesystem paths in
  [ResponseBuilder.cpp](/home/ily4s/Desktop/WEBSERV/srcs/http/ResponseBuilder.cpp:200).
- **Problem:** The tests read an outside file through both a final-file symlink
  and a directory symlink. POST also overwrites an outside symlink target.
- **Fix:** Safely check/open every path component and enforce the root/upload
  boundary, including index and custom-error paths. Refuse unsafe links and
  define an explicit existing-file overwrite policy, using allowed functions.
- **If skipped:** Files can be exposed or damaged outside the intended website
  directories. All reproductions used disposable files, not user data.

### 3. [P1] A configured CGI index is served as source

- **What this is:** The directory index branch in
  [ResponseBuilder.cpp](/home/ily4s/Desktop/WEBSERV/srcs/http/ResponseBuilder.cpp:213).
- **Problem:** With `index index.py` and a `.py` interpreter mapping, GET `/cgi/`
  returns the Python source as a normal 200 response instead of executing it.
- **Fix:** Route the resolved CGI index through CgiHandler, or restore an
  equivalent Core/CGI index interception before static reading. Preserve route
  policy, query and script/path-info semantics.
- **If skipped:** Supported CGI/index combinations can disclose script source.
  The provided default CGI location does not currently set a Python index, but
  the exposed configuration combination is supported and was tested explicitly.

### 4. [P2] A valid 33 MiB static file receives no HTTP response

- **What this is:** Whole-file allocation and serialization in
  [ResponseBuilder::_readFile](/home/ily4s/Desktop/WEBSERV/srcs/http/ResponseBuilder.cpp:440),
  followed by Core's buffer accounting.
- **Problem:** A sparse 33 MiB file is read/copied into response storage, exceeds
  the accounted budget and the connection closes before an HTTP status arrives.
- **Fix:** Connect ordinary GET/index responses to the existing tested queueFile
  transport instead of constructing all file/response copies first. Use bounded
  error-document reading and return an explicit refusal when admission fails.
- **If skipped:** Legitimate large static files fail despite the streaming Core
  helper working. This is not evidence that the 64 MiB budget is a hard OS RSS cap.

### 5. [P2] Connection policy and pipelined responses regress

- **What this is:** Response dispatch in
  [Server::buildResponseForClient](/home/ily4s/Desktop/WEBSERV/srcs/core/Server.cpp:407)
  and connection tokens in
  [HttpRequest::keepAlive](/home/ily4s/Desktop/WEBSERV/srcs/http/HttpRequest.cpp:52).
- **Problem:** Ordinary requests keep Client's default close flag, so a second
  pipelined GET gets EOF. CGI also treats `Connection: keep-alive, CLOSE` as
  keep-alive because it compares only whole header strings.
- **Fix:** Set Core's close/reuse policy from parsed connection tokens before
  dispatch, honor response-side close requests, and preserve/reset pipeline input
  correctly. Do not reserialize CGI output through the single-value header map.
- **If skipped:** Connection reuse is inconsistent, queued subsequent requests
  are lost, and CGI can contradict an explicit close token. Browsers may conceal
  some ordinary GET failures by retrying on another connection.

### 6. [P2] Request-header validation is incomplete

- **What this is:** [RequestParser::_parseHeaders](/home/ily4s/Desktop/WEBSERV/srcs/http/RequestParser.cpp:126).
- **Problem:** Empty Host and a control character inside a header receive 200.
  More than 40 KiB of complete header lines also bypass the intended 32 KiB
  check, which only inspects pending incomplete lines.
- **Fix:** Validate header names/values and required Host content, and count all
  consumed headers plus trailers against an aggregate limit. Keep the now-working
  duplicate/conflicting framing and decimal-overflow checks.
- **If skipped:** Malformed messages are accepted and the intended header bound
  is ineffective. Removing the old adapter made the backend the live boundary.

### 7. [P2] Chunked trailers are mistaken for a following request

- **What this is:** Final-chunk handling in
  [RequestParser.cpp](/home/ily4s/Desktop/WEBSERV/srcs/http/RequestParser.cpp:327).
- **Problem:** Two trailers followed by a valid pipelined request consume only
  the first trailer; the next HTTP response is 400 rather than the expected 200.
- **Fix:** Validate and consume the full trailer section through its final empty
  line, incrementally across feeds, while retaining only actual next-request bytes.
- **If skipped:** Valid chunked requests corrupt subsequent request boundaries.
  The old split-zero-chunk hang is fixed; this is a separate remaining defect.

### 8. [P2] Directory listings insert raw filenames into HTML

- **What this is:** [ResponseBuilder::_handleAutoindex](/home/ily4s/Desktop/WEBSERV/srcs/http/ResponseBuilder.cpp:347).
- **Problem:** A filename containing an img/onerror tag appears directly in the
  returned HTML, including the generated link.
- **Fix:** Escape HTML text/attributes and separately encode URL path components;
  apply the same care to the displayed directory path.
- **If skipped:** A listing can turn an attacker-controlled filename into browser
  content. This applies to an enabled listing without an overriding index page.

### 9. [P2] DELETE's 204 has forbidden Content-Length framing

- **What this is:** [HttpResponse::serialize](/home/ily4s/Desktop/WEBSERV/srcs/http/HttpResponse.cpp:50).
- **Problem:** Successful DELETE emits `204` with `Content-Length: 0` because the
  serializer automatically adds the header for every status.
- **Fix:** Give bodyless statuses appropriate serialization rules; omit both
  Content-Length and body for 204, and handle other special statuses individually.
- **If skipped:** The shared HTTP serializer still emits invalid framing even
  though the separate CGI bodyless-response test passes.

### 10. [P2] Failed upload writes still report 201

- **What this is:** Writes/close followed by unconditional success in
  [ResponseBuilder::_handlePOST](/home/ily4s/Desktop/WEBSERV/srcs/http/ResponseBuilder.cpp:292).
- **Problem:** A disposable destination pointing at `/dev/full` rejects writes,
  but the server reports 201. Stream write/close results are not checked.
- **Fix:** Verify write and close success, choose failure cleanup/overwrite
  behavior, and report an error instead of successful creation.
- **If skipped:** Clients can be told their upload was saved when it was not.

## Should fix

### 11. [P2] CGI script names beginning with a dash become interpreter options

- **What this is:** The interpreter argument in
  [CgiLauncher.cpp](/home/ily4s/Desktop/WEBSERV/srcs/cgi/CgiLauncher.cpp:178).
- **Problem:** An existing valid `-option.py` script returns 502 because the
  basename is passed directly as an interpreter argument.
- **Fix:** Prefix a leading-dash basename with `./` in the interpreter argument,
  while retaining the correct script metadata and working directory.
- **If skipped:** A valid filename is treated as a command-line option rather
  than the selected script.

### 12. [P2] Server-generated CGI errors ignore configured documents

- **What this is:** [CgiResponse::onCgiError](/home/ily4s/Desktop/WEBSERV/srcs/cgi/CgiResponse.cpp:251).
- **Problem:** A missing CGI script returns plain `Not Found` despite the site's
  valid configured 404 document being used by ordinary HTTP errors.
- **Fix:** Apply the selected site's mapping with safe bounded regular-file
  reading and fallback. Keep a valid CGI-authored error response's own body.
- **If skipped:** Custom errors work inconsistently between HTTP and CGI.

### 13. [P2] Static directory URLs lack a slash redirect

- **What this is:** The directory branch of
  [ResponseBuilder::_handleGET](/home/ily4s/Desktop/WEBSERV/srcs/http/ResponseBuilder.cpp:213).
- **Problem:** GET `/folder?x=y` does not redirect to `/folder/?x=y`; the tested
  empty directory returns 403. Direct directory-index responses also omit the
  URL normalization that relative links depend on.
- **Fix:** Add a directory slash redirect with correctly encoded path and retained
  query, then serve/index/list the normalized URL.
- **If skipped:** Directory behavior differs from the intended policy and
  browser-relative links can resolve against the wrong parent path.

### 14. [P2, bonus] Cookies overwrite and session state is not demonstrated

- **What this is:** [HttpResponse::setCookie](/home/ily4s/Desktop/WEBSERV/srcs/http/HttpResponse.cpp:124)
  and the [auth handlers](/home/ily4s/Desktop/WEBSERV/srcs/sessions/AuthHandlers.cpp:10).
- **Problem:** Two setCookie calls emit one field. Login/logout now create and
  destroy sessions, but `getSession` has no application caller, so subsequent
  requests do not demonstrate retrieval/use of the stored user state.
- **Fix:** Preserve repeated Set-Cookie fields and coordinate the representation
  with CGI. Add a small session-state consumer, such as a current-user or visit
  route, and verify reconnect, expiry, invalid IDs and logout.
- **If skipped:** The passing browser cookie demonstration is mistaken for a
  complete persistent-session feature. The manager's isolated lookup/expiry
  functions themselves passed the probe.

### 15. [P2, bonus] Session identifiers are deterministic and storage is unbounded

- **What this is:** [SessionManager](/home/ily4s/Desktop/WEBSERV/srcs/sessions/SessionManager.cpp:6).
- **Problem:** Time-seeded rand produces identical first IDs in separately
  initialized managers in the probe. There is no entry cap, collision retry or
  inclusion of session storage in Core's buffer accounting; repeated successful
  logins without an old cookie keep adding entries until expiry.
- **Fix:** Use unpredictable IDs with an allowed-function design, retry collisions
  rather than overwriting entries, and bound/clean session storage. Decide whether
  websites share or isolate sessions; the current runtime has one manager.
- **If skipped:** IDs are predictable and enabling the demo adds memory growth
  outside the advertised Core buffer policy. Predictable generation was tested;
  an actual session takeover and long-duration exhaustion were not attempted.

### 16. [P3, bonus] Equivalent form data rejects valid demo credentials

- **What this is:** [handleLogin](/home/ily4s/Desktop/WEBSERV/srcs/sessions/AuthHandlers.cpp:18).
- **Problem:** `password=demo-only&user=demo` returns 401, while the same fields in
  the hardcoded order succeed. The handler compares the whole raw body string.
- **Fix:** Parse/decode the form fields and validate their values independently
  of ordering, with a clear duplicate-field and content-type policy.
- **If skipped:** Valid form submissions from another client can fail even though
  the bundled browser form happens to use the expected order.

### 17. [P3] Configuration validation and previous documented grammar disagree

- **What this is:** Directive validation in
  [Config.cpp](/home/ily4s/Desktop/WEBSERV/srcs/config/Config.cpp:287).
- **Problem:** Six historical strict-grammar cases are now accepted: a relative
  redirect, three hostless HTTP(S) URL forms, and index values containing `?`/`%`.
- **Fix:** Agree the intended grammar. Restore checks for forms the project
  rejects, or update documented syntax/tests where a deliberate relaxation is
  supported end to end. For example, relative Location references can be valid
  HTTP, so that one acceptance is not automatically a protocol bug.
- **If skipped:** Notes/tests and the parser describe different contracts. This
  is distinct from the reproduced server-availability and file-safety blockers.

## Improvements confirmed in this import

- 405 now retains Allow and serializes it correctly.
- Decimal Content-Length overflow, duplicate framing fields, simultaneous
  Content-Length/Transfer-Encoding and encoded invalid paths are rejected in
  the exercised cases.
- The split final zero chunk no longer hangs.
- Sessions compile and are included in the build; login/logout actually call the
  manager and issue/expire a browser cookie.
- Static binary content, binary upload/GET/DELETE, two-site context, body limits,
  Python/Bash CGI, CGI metadata/cwd, repeated CGI cookies, local redirects,
  CGI failure/timeout/reaping and Core resource cleanup all have passing checks.

## Environment, subject and coverage limits

- `/usr/bin/php-cgi` is not installed here. The PHP interpreter test is skipped;
  the default config advertises it, so install it on the evaluation machine or
  adjust that example. Python and Bash CGI pass. Multiple CGI types are bonus;
  the missing PHP installation does not invalidate the working Python CGI.
- The subject requires nonblocking live I/O and server availability. Findings
  1–3 should be addressed first. Not every optional persistence, strict grammar
  or bonus check in this report is a separate mandatory grading requirement.
- Local Core tests cover descriptor/memory admission and recovery. The HTTP
  stress check is a short 160-request workload, not a multi-hour soak or
  internet-scale benchmark. No packet-loss/network-fault campaign was run.
- Chromium was tested. Firefox, macOS, NGINX comparison and the school's external
  tester were not run. PHP-CGI was not installed for this audit.
- The source branch has no root README. The retained local README still needs
  the two actual 42 logins, intentionally deferred at the user's request.
- Retained earlier notes describe previous code, including the removed adapter;
  use this report and the imported source for current integration status.

## Reproduce

```sh
make re
make test
python3 tests/test_merged_project.py
python3 tests/test_http_pending.py
python3 tests/run_config_tests.py
python3 tests/run_http_parser_tests.py
python3 tests/run_core_tests.py
python3 tests/test_resources.py
python3 tests/test_cgi_launch.py
python3 tests/test_cgi_response.py
python3 tests/test_cgi_core.py
python3 tests/test_startup.py
python3 tests/test_config_compat.py
python3 tests/test_examples.py

c++ -std=c++98 -Wall -Wextra -Werror -Iincludes \
  srcs/http/HttpResponse.cpp srcs/sessions/Session.cpp \
  srcs/sessions/SessionManager.cpp tests/session_response_audit.cpp \
  -o /tmp/webserv-session-response-check
/tmp/webserv-session-response-check
```

Browser automation is in `tests/test_merged_browser.py`; it needs Python
Playwright/Chromium, a running server, its URL and a screenshot output directory.
The recorded run used disposable copies of the shipped web/CGI assets, so it did
not leave uploaded test files in the project website.

**Verdict:** imported successfully; fix the availability, file-safety and CGI-index
issues first, then rerun the broader tests. The project is not ready to call done.

**Not checked:** the external/environment/long-duration checks listed above;
source observations about session growth were not an exhaustion test. Passing
smoke tests or sanitizer checks is not proof that these functional failures are fixed.

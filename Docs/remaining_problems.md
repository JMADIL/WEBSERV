# Remaining problems after integrating Adil's update

Reviewed and imported commit `7439f6b` on 2026-11-01.
This is the current checklist; earlier review documents describe older snapshots.

## Integration status

Imported the 14 HTTP/MIME/session source files and matching headers exactly as
provided. Adil's external checkout was not edited. Our current Config, Core,
adapter, CGI and Makefile were preserved; his older copies did not replace them.

The merged mandatory executable builds. Session sources now compile separately,
but remain outside the mandatory build because HTTP does not use them yet.
The adapter remains necessary: adding the backend's new API does not fix all
of its framing, URI and header validation issues.

Fixed by this update:

- The raw parser no longer hangs on a split terminating zero chunk.
- RequestParser now supplies `PARSE_HEADERS_COMPLETE`, `setBodyLimit` and
  `getErrorCode`; it also guards chunk-size arithmetic and body limits.
- Session header/source preprocessor errors are fixed; both session sources
  pass the C++98 compilation check.
- The old Core/parser compile mismatch is avoided in our project by retaining
  our current adapter-based Core. Do not import Adil's older Core over it.

## Reproduced failures: fix these first

`make test-http` currently reports eight failures: problems 1–7 below, with
problem 2 tested separately for final-file and intermediate-directory links.
The session compilation and split-zero-chunk checks now pass.

### 1. Raw chunked parser leaves trailers before the next request

- **Owner:** Adil — request parsing.
- **File:** `srcs/http/RequestParser.cpp`, zero-chunk handling around lines 308–345.
- **Evidence:** After `0\r\n`, sending two trailer fields followed by a new GET
  causes completion after the first field. The second trailer remains before
  the GET. The probe is `tests/adil_parser_probe.cpp`, argument `trailers`.
- **Effect:** A following request can be parsed incorrectly.
- **Fix:** Parse and validate the entire trailer section through its final empty
  line, incrementally across feeds. Refuse forbidden framing/routing trailers;
  leave only the following request in the buffer.
- **Current protection:** Our adapter validates/discards trailers before sending
  a canonical terminating chunk to this parser. The live Core path is guarded.

### 2. GET follows file and directory links outside the configured root

- **Owner:** Adil — HTTP file handlers.
- **File:** `srcs/http/ResponseBuilder.cpp`, `_handleGET`, `_resolvePath`,
  `_fileExists`, `_isDirectory`, `_readFile`.
- **Evidence:** Both a file symlink and an intermediate directory symlink return
  `200` with content outside the configured root. These are two failing checks.
- **Effect:** Private files can be exposed.
- **Fix:** Enforce containment and safely check/open every path component,
  including index and custom error files. Refuse unsafe links. Use only functions
  allowed by the subject; the existing CGI checks show our current trusted-root
  policy. Do not rely only on joining strings or checking that a path exists.

### 3. An upload can overwrite a symlink target

- **Owner:** Adil — POST/upload handler.
- **File:** `srcs/http/ResponseBuilder.cpp`, `_handlePOST`, around line 279.
- **Evidence:** POST to a destination that is a symlink overwrites the outside
  target. Tests use disposable files inside a temporary directory.
- **Effect:** An upload can destroy another file.
- **Fix:** Open the destination safely, refuse unsafe links/special files and
  define whether existing regular files may be replaced. Verify the upload
  directory's components as well. Pair this with problem 10's write checks.

### 4. GET on a FIFO blocks the whole server

- **Owner:** Adil — file opening/reading.
- **File:** `srcs/http/ResponseBuilder.cpp`, `_readFile`, around line 436.
- **Evidence:** Requesting a FIFO times out; the single server thread waits in
  its ordinary file reader. The test must kill that server instance.
- **Effect:** Other clients cannot receive service. This conflicts with the
  subject's nonblocking requirement.
- **Fix:** Open safely without blocking and verify that content is a regular
  file before reading it. Refuse FIFOs/devices instead of treating them as static
  files. Only regular disk files receive the subject's poll exemption.

### 5. Static directory requests lack a slash redirect

- **Owner:** Adil — GET directory handling.
- **File:** `srcs/http/ResponseBuilder.cpp`, `_handleGET`, around line 207.
- **Evidence:** `/empty?x=y` does not redirect to `/empty/?x=y`.
- **Effect:** Relative browser links can resolve against the wrong directory.
- **Fix:** Redirect directory URLs that lack the trailing slash, preserve the
  query, and encode the path correctly. Test directories with and without an
  index and with spaces in their names.
- **Already handled:** Our CGI directory-index redirect follows this policy.

### 6. A 405 response loses its Allow header

- **Owner:** Adil — response construction.
- **File:** `srcs/http/ResponseBuilder.cpp`, around lines 66–67.
- **Evidence:** A disallowed POST receives 405 without `Allow: GET`.
- **Cause:** `Allow` is set before `buildErrorResponse`, which resets the response.
- **Fix:** Build the error, add `Allow`, then serialize the final result. Ensure
  the returned wire buffer includes the added header, including custom errors.

### 7. Autoindex inserts filenames directly into HTML

- **Owner:** Adil — directory listing.
- **File:** `srcs/http/ResponseBuilder.cpp`, `_handleAutoindex`, around line 397.
- **Evidence:** A filename containing `<img ...>` appears as raw HTML.
- **Effect:** A filename can inject browser content into the directory listing.
- **Fix:** Escape displayed HTML and encode URL path components separately.
  Escape the displayed directory path too. HTML escaping and URL encoding are
  different operations; do not substitute one for the other.

## Further findings from reading the code

These are separate from the eight failing checks above. They need their own
verification when fixed; they are not additional claims of passing/failing
automated tests in this run.

### 8. Static files are fully buffered before Core can enforce its budget

- **Owner:** Adil's HTTP file handlers, using our Core streaming interface.
- **File:** `srcs/http/ResponseBuilder.cpp`, GET/index/error reads and `_readFile`.
- **Problem:** The reader allocates a vector for the entire file, then response
  construction/serialization creates more storage. A very large file can exceed
  the intended memory budget before Core checks the completed response.
- **Fix:** Use the tested `Client::queueFile` transport with a verified regular
  file descriptor and correct headers. Ownership transfers only on success;
  callers close the descriptor on a failed handoff. Use bounded reading and
  fallback for custom error documents. Core's 9 MiB streaming test already
  verifies its transport helper, not the current production GET implementation.

### 9. File read failure can become a successful empty response

- **Owner:** Adil — `_readFile` and its callers.
- **Problem:** A valid empty file, open failure, bad seek and failed read all
  return an empty vector. GET then sets status 200.
- **Effect:** An unreadable or incompletely read file can look successfully served.
- **Fix:** Distinguish successful empty content from failure. Return an accurate
  error before sending headers; if a streamed file fails after headers have gone
  out, close the response rather than claiming the promised body was delivered.

### 10. Upload write/close failures are ignored

- **Owner:** Adil — `_handlePOST`, around lines 288–292.
- **Problem:** After opening the destination, the code writes/closes and always
  reports 201, without checking stream failure. A full disk can trigger this.
- **Fix:** Check write and close results. Report an error on failure, and use a
  deliberate cleanup/overwrite policy so failed uploads do not silently damage
  existing content. Test an injected write failure.

### 11. Shared response serialization has incorrect bodyless framing

- **Owner:** Adil — `srcs/http/HttpResponse.cpp`, `serialize`, around line 50.
- **Problem:** Content-Length is automatically added for every response, including
  DELETE's 204. Some statuses prohibit this header/body combination.
- **Fix:** Handle bodyless status rules explicitly, especially 204 and 304, and
  suppress forbidden bodies. Test the exact response bytes and connection reuse.
- **Already handled:** Our CGI serializer has separate bodyless-response checks.

### 12. Raw parser validation still depends on the adapter

- **Owner:** Adil — `srcs/http/RequestParser.cpp`.
- **Problem:** Duplicate Content-Length fields are overwritten; Transfer-Encoding
  takes precedence without refusing simultaneous Content-Length. Numeric stream
  extraction does not check decimal overflow. Header size checks do not cap the
  entire consumed section. URI decoding permits malformed escapes/NUL.
- **Fix:** Validate framing before overwriting fields, reject conflicts and
  overflow, enforce aggregate limits, and validate decoded paths exactly once.
  Add focused raw-parser tests. Do not remove the adapter merely because the new
  header/body-limit API exists; it still provides these protections for Core.

### 13. Missing shared reason phrases

- **Owner:** Adil — `srcs/http/HttpResponse.cpp`, status text switch.
- **Problem:** 417 and 503 produce `Unknown Status` in shared HTTP errors.
- **Fix:** Add `Expectation Failed` and `Service Unavailable`. This is lower
  priority than framing/availability; the numeric code is already present.

## Bonus work: separate from mandatory readiness

### 14. Sessions compile but are not connected to HTTP

- **Owner:** Adil — bonus sessions/cookies.
- **Evidence:** No HTTP/Core request path creates or looks up a SessionManager
  session; those calls currently occur only within the session implementation.
- **Fix:** Connect cookie parsing, session lookup/creation, expiry and response
  cookies. Add the session sources to the build when used, and supply a small
  demonstration showing state persists across requests and expires correctly.
  A successful syntax check does not demonstrate the bonus works.

### 15. Repeated server cookies overwrite each other

- **Owner:** Adil — `HttpResponse::setCookie` and response header storage.
- **Problem:** All calls write the same `Set-Cookie` key in a map; only the last
  cookie survives.
- **Fix:** Represent repeated Set-Cookie fields without overwriting them and
  serialize each independently. Coordinate any shared response-type change with
  our CGI serializer so existing server cookies and repeated CGI cookies survive.

### 16. Session identifiers are predictable and collisions are unchecked

- **Owner:** Adil — `SessionManager::_generateSessionId` and `createSession`.
- **Problem:** Each manager seeds global rand with the current second and builds
  IDs from time plus rand. Two managers created in the same second can generate
  the same first ID; insertion also replaces an existing entry on a collision.
- **Fix:** Choose an unpredictable generation method consistent with the allowed
  functions and retry collisions instead of overwriting a session. Bound session
  storage and perform expiry cleanup as part of the actual request flow.

## Final team checks before evaluation

- The README's two author placeholders are intentionally still pending because
  you asked to skip the logins. Fill them before submission.
- Run browser tests, NGINX behavior comparisons and a sustained whole-server
  stress test after the HTTP fixes. The school tester, macOS and PHP-CGI were not
  checked here. Python is the interpreter currently demonstrated.
- Recheck the subject against the exact final merged commit, including required
  examples. Do not describe the complete project as finished based only on the
  Config/Core/CGI tests or on session compilation.


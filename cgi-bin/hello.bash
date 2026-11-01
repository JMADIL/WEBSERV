printf 'Status: 200 OK\r\n'
printf 'Content-Type: text/plain; charset=utf-8\r\n'
printf '\r\n'
printf 'Bash CGI works\n'
printf 'Method: %s\n' "${REQUEST_METHOD:-}"
printf 'Query: %s\n' "${QUERY_STRING:-}"
printf 'Path info: %s\n' "${PATH_INFO:-}"
if [ "${REQUEST_METHOD:-}" = POST ]; then
    printf 'Body: '
    cat
    printf '\n'
fi

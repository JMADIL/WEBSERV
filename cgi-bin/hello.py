import os
import sys


body = sys.stdin.buffer.read().decode("utf-8", errors="replace")

print("Status: 200 OK")
print("Content-Type: text/plain; charset=utf-8")
print()
print("Python CGI works")
print("Method: " + os.environ.get("REQUEST_METHOD", ""))
print("Query: " + os.environ.get("QUERY_STRING", ""))
print("Path info: " + os.environ.get("PATH_INFO", ""))
if body:
    print("Body: " + body)

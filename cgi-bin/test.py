#!/usr/bin/python3
"""Evaluation demo: GET query/PATH_INFO and binary POST, read until EOF."""
import json
import os
import sys

body = sys.stdin.buffer.read()
print("Content-Type: application/json")
print()
print(json.dumps({
    "message": "CGI works",
    "method": os.environ.get("REQUEST_METHOD"),
    "query": os.environ.get("QUERY_STRING"),
    "script": os.environ.get("SCRIPT_NAME"),
    "path_info": os.environ.get("PATH_INFO"),
    "content_length": os.environ.get("CONTENT_LENGTH"),
    "body_hex": body.hex(),
}, indent=2))

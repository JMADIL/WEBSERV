<?php
header('Content-Type: text/plain; charset=utf-8');

echo "PHP CGI works\n";
echo "Method: " . (getenv('REQUEST_METHOD') ?: '') . "\n";
echo "Query: " . (getenv('QUERY_STRING') ?: '') . "\n";
echo "Path info: " . (getenv('PATH_INFO') ?: '') . "\n";

$body = file_get_contents('php://stdin');
if ($body !== false && $body !== '') {
    echo "Body: " . $body . "\n";
}

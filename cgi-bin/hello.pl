#!/usr/bin/perl
use strict;
use warnings;

sub esc {
    my $s = shift;
    $s =~ s/&/&amp;/g;
    $s =~ s/</&lt;/g;
    $s =~ s/>/&gt;/g;
    return $s;
}

my $method = $ENV{REQUEST_METHOD} || "N/A";
my $query  = $ENV{QUERY_STRING}   || "";
my $len    = $ENV{CONTENT_LENGTH} || 0;
my $body   = "";

if ($method eq "POST" && $len > 0) {
    my $got = 0;
    while ($got < $len) {
        my $n = read(STDIN, $body, $len - $got, $got);
        last unless $n;
        $got += $n;
    }
}

print "Content-Type: text/html\r\n";
print "\r\n";
print "<html><body>\n";
print "<h1>Perl CGI works!</h1>\n";
print "<p>Method: " . esc($method) . "</p>\n";
print "<p>Query: " . esc($query) . "</p>\n";
print "<p>Body: " . esc($body) . "</p>\n" if $method eq "POST";
print "</body></html>\n";
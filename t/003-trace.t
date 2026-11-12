# vim:set ft=perl ts=4 sw=4 et:

# /cdn-cgi/trace: 仅在 cdn_node_colo 与 cdn_node_sliver 均配置时启用

use Test::Nginx::Socket 'no_plan';

repeat_each(2);

no_long_string();

# 各用例自带 location /, 不要 Test::Nginx 再注入一个 (否则 duplicate location)
no_root_location();

# 静态文件 html/cdn-cgi/trace 作为 trace 未接管时的回退内容
add_block_preprocessor(sub {
    my $block = shift;

    if (!defined $block->user_files) {
        $block->set_value("user_files", <<'_EOC_');
>>> cdn-cgi/trace
static file
_EOC_
    }

    if (!defined $block->config) {
        $block->set_value("config", <<'_EOC_');
    location / {
        root html;
        default_type text/html;
    }
_EOC_
    }
});

run_tests();

__DATA__

=== TEST 1: trace output
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/trace
--- more_headers
User-Agent: test-agent/1.0
--- response_headers
Content-Type: text/plain
Cache-Control: no-store
X-Content-Type-Options: nosniff
--- response_body_like eval
qr{\Ah=localhost
ip=127\.0\.0\.1
ts=\d+\.\d{2}
visit_scheme=http
uag=test-agent/1\.0
colo=HKG
sliver=010-tier1
http=HTTP/1\.1
tls=off
\z}
--- no_error_log
[error]



=== TEST 2: Content-Length matches body
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- raw_request eval
"GET /cdn-cgi/trace HTTP/1.0\r
Host: localhost\r
User-Agent: ua\r
\r
"
--- response_body_like eval
qr{\Ah=localhost\nip=127\.0\.0\.1\nts=\d+\.\d{2}\nvisit_scheme=http\nuag=ua\ncolo=HKG\nsliver=010-tier1\nhttp=HTTP/1\.0\ntls=off\n\z}
--- response_headers_like
Content-Length: \d+
--- no_error_log
[error]



=== TEST 3: missing User-Agent gives empty uag
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- raw_request eval
"GET /cdn-cgi/trace HTTP/1.0\r
Host: localhost\r
\r
"
--- response_body_like eval
qr{\nuag=\ncolo=HKG\n}
--- no_error_log
[error]



=== TEST 4: Host header is echoed as-is
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/trace
--- more_headers
Host: www.Example.com
--- response_body_like eval
qr{\Ah=www\.example\.com\n}i
--- no_error_log
[error]



=== TEST 5: empty colo and sliver still enable trace
--- http_config
    cdn_node_colo "";
    cdn_node_sliver "";
--- request
GET /cdn-cgi/trace
--- response_body_like eval
qr{\ncolo=\nsliver=\n}
--- response_headers
Content-Type: text/plain
--- no_error_log
[error]



=== TEST 6: only colo configured: trace disabled
--- http_config
    cdn_node_colo HKG;
--- request
GET /cdn-cgi/trace
--- response_body
static file
--- response_headers
!Cache-Control
!X-Content-Type-Options
--- no_error_log
[error]



=== TEST 7: only sliver configured: trace disabled
--- http_config
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/trace
--- response_body
static file
--- no_error_log
[error]



=== TEST 8: neither configured: trace disabled
--- request
GET /cdn-cgi/trace
--- response_body
static file
--- no_error_log
[error]



=== TEST 9: query string does not affect matching
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/trace?foo=bar
--- response_body_like eval
qr{\Ah=localhost\n.*\ncolo=HKG\n}s
--- no_error_log
[error]



=== TEST 10: exact match only: trailing slash is not trace
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/trace/
--- error_code: 404
--- response_body_unlike
colo=HKG



=== TEST 11: exact match only: longer path is not trace
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/tracex
--- error_code: 404
--- response_body_unlike
colo=HKG



=== TEST 12: match is case-sensitive
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /CDN-CGI/TRACE
--- error_code: 404
--- response_body_unlike
colo=HKG



=== TEST 13: normalized URI is matched (dot segments, merged slashes)
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET //cdn-cgi/./foo/../trace
--- response_body_like eval
qr{\ncolo=HKG\n}
--- no_error_log
[error]



=== TEST 14: HEAD returns headers without body
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
HEAD /cdn-cgi/trace
--- response_headers
Content-Type: text/plain
Cache-Control: no-store
X-Content-Type-Options: nosniff
--- response_body
--- no_error_log
[error]



=== TEST 15: POST is rejected with 405
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
POST /cdn-cgi/trace
hello
--- error_code: 405
--- response_body_unlike
colo=HKG



=== TEST 16: GET with a request body is discarded and served
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/trace
some body
--- response_body_like eval
qr{\ncolo=HKG\n}
--- no_error_log
[error]



=== TEST 17: trace takes precedence over content handlers (proxy_pass)
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- config
    location /cdn-cgi/ {
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend;
    }
    location = /backend {
        return 200 "backend\n";
    }
--- request
GET /cdn-cgi/trace
--- response_body_like eval
qr{\ncolo=HKG\n}
--- no_error_log
[error]



=== TEST 18: access phase runs before trace (deny)
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- config
    location /cdn-cgi/ {
        deny all;
    }
--- request
GET /cdn-cgi/trace
--- error_code: 403
--- response_body_unlike
colo=HKG



=== TEST 19: access phase runs before trace (allow)
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- config
    location /cdn-cgi/ {
        allow 127.0.0.1;
        deny all;
    }
--- request
GET /cdn-cgi/trace
--- response_body_like eval
qr{\ncolo=HKG\n}
--- no_error_log
[error]



=== TEST 20: internal redirect to /cdn-cgi/trace is not served by trace
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- config
    location / {
        root html;
        default_type text/plain;
    }
    location = /go {
        error_page 418 = /cdn-cgi/trace;
        return 418;
    }
--- request
GET /go
--- response_body
static file
--- no_error_log
[error]



=== TEST 21: subrequest to /cdn-cgi/trace is not served by trace
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- config
    location / {
        root html;
        default_type text/plain;
    }
    location = /page.html {
        root html;
        ssi on;
    }
--- user_files
>>> cdn-cgi/trace
static file
>>> page.html
[<!--# include virtual="/cdn-cgi/trace" -->]
--- request
GET /page.html
--- response_body
[static file
]
--- no_error_log
[error]



=== TEST 22: configured Cache-Control / X-Content-Type-Options are not duplicated
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
    cdn_node_header Cache-Control "public, max-age=60";
    cdn_node_header x-content-type-options nosniff;
    cdn_node_header X-Node node-1;
--- request
GET /cdn-cgi/trace
--- response_headers
Cache-Control: no-store
X-Content-Type-Options: nosniff
X-Node: node-1
--- raw_response_headers_unlike chomp
(?si)(?:Cache-Control:.*Cache-Control:|X-Content-Type-Options:.*X-Content-Type-Options:|public)
--- no_error_log
[error]



=== TEST 23: configured Cache-Control still applies to non-trace responses
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
    cdn_node_header Cache-Control "public, max-age=60";
--- config
    location = /t {
        return 200 "ok\n";
    }
--- request
GET /t
--- response_headers
Cache-Control: public, max-age=60
--- no_error_log
[error]



=== TEST 24: X-Status on trace reflects 200
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
    cdn_node_header X-Status $status;
--- request
GET /cdn-cgi/trace
--- response_headers
X-Status: 200
--- no_error_log
[error]



=== TEST 25: long User-Agent is echoed in full
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- request
GET /cdn-cgi/trace
--- more_headers eval
"User-Agent: " . ("A" x 4000)
--- response_body_like eval
"\nuag=" . ("A" x 4000) . "\ncolo=HKG\n"
--- no_error_log
[error]



=== TEST 26: trace over keepalive, followed by a normal request
--- http_config
    cdn_node_colo HKG;
    cdn_node_sliver 010-tier1;
--- config
    location / {
        root html;
        default_type text/plain;
    }
    location = /t {
        return 200 "ok\n";
    }
--- pipelined_requests eval
["GET /cdn-cgi/trace", "GET /t"]
--- response_body_like eval
[qr{\ncolo=HKG\n}, qr{\Aok\n\z}]
--- no_error_log
[error]

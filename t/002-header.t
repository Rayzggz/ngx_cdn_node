# vim:set ft=perl ts=4 sw=4 et:

# cdn_node_header: 响应头过滤器

use Test::Nginx::Socket 'no_plan';

repeat_each(2);

no_long_string();
run_tests();

__DATA__

=== TEST 1: static value on return
--- http_config
    cdn_node_header X-Node node-1;
--- config
    location = /t {
        return 200 "ok\n";
    }
--- request
GET /t
--- response_headers
X-Node: node-1
--- response_body
ok
--- no_error_log
[error]



=== TEST 2: multiple headers keep configured order and name case
--- http_config
    cdn_node_header x-first 1;
    cdn_node_header X-Second 2;
    cdn_node_header X-THIRD 3;
--- config
    location = /t {
        return 200 "ok\n";
    }
--- request
GET /t
--- raw_response_headers_like
x-first: 1\r\nX-Second: 2\r\nX-THIRD: 3\r\n
--- no_error_log
[error]



=== TEST 3: header added to static file response
--- http_config
    cdn_node_header X-Node node-1;
--- config
    location /static/ {
        root html;
    }
--- user_files
>>> static/a.txt
hello
--- request
GET /static/a.txt
--- response_headers
X-Node: node-1
Content-Type: text/plain
--- response_body
hello
--- no_error_log
[error]



=== TEST 4: header added to 404 error page
--- http_config
    cdn_node_header X-Node node-1;
    cdn_node_header X-Status $status;
--- config
    location /static/ {
        root html;
    }
--- request
GET /static/missing.txt
--- error_code: 404
--- response_headers
X-Node: node-1
X-Status: 404
--- error_log
open()



=== TEST 5: header added to 403 from access phase
--- http_config
    cdn_node_header X-Node node-1;
--- config
    location = /t {
        deny all;
        return 200 "ok\n";
    }
--- request
GET /t
--- error_code: 403
--- response_headers
X-Node: node-1



=== TEST 6: header added after internal redirect (error_page)
--- http_config
    cdn_node_header X-Node node-1;
    cdn_node_header X-Uri $uri;
--- config
    location = /t {
        error_page 418 = /fallback;
        return 418;
    }
    location = /fallback {
        return 200 "fallback\n";
    }
--- request
GET /t
--- response_headers
X-Node: node-1
X-Uri: /fallback
--- response_body
fallback
--- no_error_log
[error]



=== TEST 7: variables are evaluated per request
--- http_config
    cdn_node_header X-Host $host;
    cdn_node_header X-Arg "arg=$arg_a";
    cdn_node_header X-Status $status;
--- config
    location = /t {
        return 201 "ok\n";
    }
--- request
GET /t?a=hello
--- more_headers
Host: example.com
--- error_code: 201
--- response_headers
X-Host: example.com
X-Arg: arg=hello
X-Status: 201
--- no_error_log
[error]



=== TEST 8: empty value is not sent (conditional via map)
--- http_config
    map $arg_on $cdn_hdr {
        default "";
        1       "enabled";
    }
    cdn_node_header X-Cond $cdn_hdr;
    cdn_node_header X-Node node-1;
--- config
    location = /t {
        return 200 "ok\n";
    }
--- request
GET /t
--- response_headers
X-Node: node-1
!X-Cond
--- no_error_log
[error]



=== TEST 9: non-empty mapped value is sent
--- http_config
    map $arg_on $cdn_hdr {
        default "";
        1       "enabled";
    }
    cdn_node_header X-Cond $cdn_hdr;
--- config
    location = /t {
        return 200 "ok\n";
    }
--- request
GET /t?on=1
--- response_headers
X-Cond: enabled
--- no_error_log
[error]



=== TEST 10: header is not duplicated by subrequests (SSI include)
--- http_config
    cdn_node_header X-Node node-1;
--- config
    location = /page.html {
        root html;
        ssi on;
    }
    location = /sub {
        return 200 "sub";
    }
--- user_files
>>> page.html
[<!--# include virtual="/sub" -->]
--- request
GET /page.html
--- response_body chomp
[sub]
--- response_headers
X-Node: node-1
--- raw_response_headers_unlike
(?s)X-Node:.*X-Node:
--- no_error_log
[error]



=== TEST 11: header coexists with add_header
--- http_config
    cdn_node_header X-Node node-1;
--- config
    location = /t {
        add_header X-Loc loc;
        return 200 "ok\n";
    }
--- request
GET /t
--- response_headers
X-Node: node-1
X-Loc: loc
--- no_error_log
[error]



=== TEST 12: header sent for HEAD request
--- http_config
    cdn_node_header X-Node node-1;
--- config
    location = /t {
        return 200 "ok\n";
    }
--- request
HEAD /t
--- response_headers
X-Node: node-1
--- response_body
--- no_error_log
[error]



=== TEST 13: no headers configured, trace disabled: module is transparent
--- config
    location = /t {
        return 200 "ok\n";
    }
--- request
GET /t
--- response_headers
!X-Node
!Cache-Control
--- response_body
ok
--- no_error_log
[error]



=== TEST 14: header in proxied response, with upstream variable
--- http_config
    cdn_node_header X-Node node-1;
    cdn_node_header X-Upstream-Status $upstream_status;
--- config
    location = /t {
        # 后端也是本 nginx, 它的响应同样带 X-Node, 隐藏掉以免透传造成重复
        proxy_hide_header X-Node;
        proxy_pass http://127.0.0.1:$TEST_NGINX_SERVER_PORT/backend;
    }
    location = /backend {
        return 202 "backend\n";
    }
--- request
GET /t
--- error_code: 202
--- response_body
backend
--- raw_response_headers_like
(?s)X-Node: node-1\r\n.*X-Upstream-Status: 202\r\n
--- raw_response_headers_unlike
(?s)X-Node:.*X-Node:
--- no_error_log
[error]

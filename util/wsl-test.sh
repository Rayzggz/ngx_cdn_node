#!/usr/bin/env bash
#
# 完整测试: 编译 (静态 + 动态模块) -> Test::Nginx -> 启动真实 nginx 用 curl 模拟请求.
# 适用于 WSL / 任意 Linux, 无需 root (Test::Nginx 缺失时用 cpanm 装到本地目录).
#
# 用法:
#   util/wsl-test.sh                 # 全部步骤
#   util/wsl-test.sh --no-dynamic    # 跳过动态模块编译测试
#   util/wsl-test.sh --smoke-only    # 只做 curl 模拟请求 (使用已编译的静态版)
#
# 环境变量:
#   NGINX_VERSION   默认 1.26.2
#   WORK_DIR        编译/运行目录, 默认 ~/.cache/ngx_cdn_node (避开 /mnt/c 的 NTFS)
#   SMOKE_PORT      smoke 测试 HTTP 端口, 默认 18080 (HTTPS 为 +1)
#   INSTALL_TEST_NGINX=1  Test::Nginx 缺失时用 cpanm 装到 $WORK_DIR/perl5 (无需 root)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NGINX_VERSION="${NGINX_VERSION:-1.26.2}"
WORK_DIR="${WORK_DIR:-$HOME/.cache/ngx_cdn_node}"
SMOKE_PORT="${SMOKE_PORT:-18080}"
SMOKE_TLS_PORT=$((SMOKE_PORT + 1))

DO_BUILD_TESTS=1
DO_DYNAMIC=1
for arg in "$@"; do
    case "$arg" in
        --no-dynamic) DO_DYNAMIC=0 ;;
        --smoke-only) DO_BUILD_TESTS=0; DO_DYNAMIC=0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

log()  { printf '\033[1;34m>>> %s\033[0m\n' "$*"; }
die()  { printf '\033[1;31mFATAL: %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- deps

log "checking build dependencies"
missing=()
for b in gcc make perl prove curl openssl; do
    command -v "$b" >/dev/null || missing+=("$b")
done
for h in pcre2.h zlib.h openssl/ssl.h; do
    echo "#include <$h>" | gcc -DPCRE2_CODE_UNIT_WIDTH=8 -E - >/dev/null 2>&1 \
        || missing+=("$h")
done
[ ${#missing[@]} -eq 0 ] || die "missing: ${missing[*]}
  sudo apt install build-essential libpcre2-dev zlib1g-dev libssl-dev curl"

# Test::Nginx: 优先系统, 否则装到 $WORK_DIR/perl5
PERL_LOCAL="$WORK_DIR/perl5"
export PERL5LIB="$PERL_LOCAL/lib/perl5${PERL5LIB:+:$PERL5LIB}"
if [ "$DO_BUILD_TESTS" = 0 ]; then
    :
elif ! perl -MTest::Nginx::Socket -e 1 2>/dev/null; then
    [ "${INSTALL_TEST_NGINX:-0}" = 1 ] || die "Test::Nginx not found. Either:
  sudo apt install libtest-nginx-perl
or let this script install it locally via cpanm (downloads from CPAN):
  INSTALL_TEST_NGINX=1 $0"
    log "installing Test::Nginx into $PERL_LOCAL (cpanm, no root)"
    mkdir -p "$WORK_DIR"
    CPANM="$(command -v cpanm || true)"
    if [ -z "$CPANM" ]; then
        CPANM="$WORK_DIR/cpanm"
        curl -fsSL https://cpanmin.us -o "$CPANM"
        chmod +x "$CPANM"
    fi
    "$CPANM" --notest --quiet -l "$PERL_LOCAL" Test::Nginx \
        || die "Test::Nginx install failed (see ~/.cpanm/build.log)"
fi
[ "$DO_BUILD_TESTS" = 0 ] \
    || perl -MTest::Nginx::Socket -e 'print "Test::Nginx $Test::Nginx::Socket::VERSION\n"'

# ---------------------------------------------------------------- sync

# 源码复制到 Linux 原生文件系统: NTFS 上编译慢, 且 CRLF/权限可能出问题
SRC_COPY="$WORK_DIR/src"
log "syncing sources to $SRC_COPY"
mkdir -p "$SRC_COPY"
rm -rf "$SRC_COPY/src" "$SRC_COPY/t" "$SRC_COPY/util" "$SRC_COPY/config"
cp -r "$ROOT/config" "$ROOT/src" "$ROOT/t" "$ROOT/util" "$SRC_COPY/"
if grep -rlI $'\r' "$SRC_COPY" >/dev/null 2>&1; then
    log "stripping CRLF from copied sources"
    grep -rlI $'\r' "$SRC_COPY" | xargs sed -i 's/\r$//'
fi

export BUILD_DIR="$WORK_DIR/build"
export TEST_NGINX_SERVROOT="$WORK_DIR/servroot"

# ---------------------------------------------------------------- build + prove

FAIL=0

run_prove() {
    local label="$1"; shift
    log "Test::Nginx ($label)"
    if env "$@" bash "$SRC_COPY/util/test.sh"; then
        log "Test::Nginx ($label): PASS"
    else
        printf '\033[1;31m!!! Test::Nginx (%s): FAIL\033[0m\n' "$label"
        FAIL=1
    fi
}

if [ "$DO_BUILD_TESTS" = 1 ]; then
    run_prove "static module"  REBUILD=1
    if [ "$DO_DYNAMIC" = 1 ]; then
        run_prove "dynamic module" REBUILD=1 DYNAMIC=1
    fi
fi

NGINX_BIN="$BUILD_DIR/nginx-$NGINX_VERSION-install/sbin/nginx"
[ -x "$NGINX_BIN" ] || BUILD_ONLY=1 bash "$SRC_COPY/util/test.sh"
# smoke-only 时源码有改动也需要重新编译
[ "$DO_BUILD_TESTS" = 1 ] || BUILD_ONLY=1 bash "$SRC_COPY/util/test.sh"
[ -x "$NGINX_BIN" ] || die "nginx binary not found: $NGINX_BIN"

# ---------------------------------------------------------------- smoke

log "smoke test: real nginx + curl ($NGINX_BIN)"

SMOKE="$WORK_DIR/smoke"
rm -rf "$SMOKE"
mkdir -p "$SMOKE"/{conf,logs,html/static,html/cdn-cgi}
echo "hello" > "$SMOKE/html/static/a.txt"
echo "static file" > "$SMOKE/html/cdn-cgi/trace"
printf '[<!--# include virtual="/sub" -->]' > "$SMOKE/html/page.html"

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj "/CN=localhost" \
    -keyout "$SMOKE/conf/key.pem" -out "$SMOKE/conf/cert.pem" 2>/dev/null

cat > "$SMOKE/conf/nginx.conf" <<EOF
worker_processes 1;
error_log logs/error.log debug;
pid logs/nginx.pid;
events { worker_connections 256; }

http {
    access_log logs/access.log;
    default_type text/plain;

    cdn_node_colo   HKG;
    cdn_node_sliver 010-tier1;

    map \$arg_on \$cond_hdr { default ""; 1 "enabled"; }

    cdn_node_header X-Node     node-1;
    cdn_node_header X-Status   \$status;
    cdn_node_header X-Upstream \$upstream_status;
    cdn_node_header X-Cond     \$cond_hdr;
    cdn_node_header Cache-Control "public, max-age=60";

    upstream backend { server 127.0.0.1:$SMOKE_PORT; }

    server {
        listen 127.0.0.1:$SMOKE_PORT;
        listen 127.0.0.1:$SMOKE_TLS_PORT ssl;
        http2 on;
        server_name localhost;
        ssl_certificate     cert.pem;
        ssl_certificate_key key.pem;
        root html;

        location = /t        { return 200 "ok\n"; }
        location = /teapot   { error_page 418 = /fallback; return 418; }
        location = /fallback { return 200 "fallback\n"; }
        location = /go-trace { error_page 418 = /cdn-cgi/trace; return 418; }
        location = /page.html { ssi on; }
        location = /sub      { return 200 "sub"; }
        location = /proxy    { proxy_hide_header X-Node; proxy_hide_header Cache-Control;
                               proxy_pass http://backend/backend; }
        location = /backend  { return 202 "backend\n"; }
        location /locked/    { deny all; }
    }
}
EOF

"$NGINX_BIN" -p "$SMOKE" -c conf/nginx.conf -t 2>&1 | sed 's/^/    /'
"$NGINX_BIN" -p "$SMOKE" -c conf/nginx.conf
trap '"$NGINX_BIN" -p "$SMOKE" -c conf/nginx.conf -s stop 2>/dev/null || true' EXIT
for _ in $(seq 50); do
    curl -s -o /dev/null "http://127.0.0.1:$SMOKE_PORT/t" && break
    sleep 0.1
done

PASS_N=0 FAIL_N=0
H="http://127.0.0.1:$SMOKE_PORT"
S="https://localhost:$SMOKE_TLS_PORT"
CURL=(curl -s -k --resolve "localhost:$SMOKE_TLS_PORT:127.0.0.1" -D "$SMOKE/hdr" -o "$SMOKE/body")

# check <name> <grep -E pattern on "headers+body"> [curl args...]
#   pattern 以 ! 开头表示必须不匹配
check() {
    local name="$1" pat="$2"; shift 2
    "${CURL[@]}" "$@" || true
    local all; all="$(tr -d '\r' < "$SMOKE/hdr"; echo '----'; cat "$SMOKE/body")"
    local ok=1
    IFS=$'\n'
    for p in $pat; do
        if [ "${p:0:1}" = "!" ]; then
            grep -Eiq -- "${p:1}" <<<"$all" && ok=0
        else
            grep -Eiq -- "$p" <<<"$all" || ok=0
        fi
    done
    unset IFS
    if [ $ok = 1 ]; then
        PASS_N=$((PASS_N+1)); printf '  \033[32mPASS\033[0m %s\n' "$name"
    else
        FAIL_N=$((FAIL_N+1)); printf '  \033[31mFAIL\033[0m %s\n' "$name"
        sed 's/^/       | /' <<<"$all" | head -30
    fi
}

dup() {  # header 出现次数必须为 1
    local name="$1" hdr="$2"; shift 2
    "${CURL[@]}" "$@" || true
    local n; n=$(grep -ic "^$hdr:" "$SMOKE/hdr" || true)
    if [ "$n" = 1 ]; then
        PASS_N=$((PASS_N+1)); printf '  \033[32mPASS\033[0m %s\n' "$name"
    else
        FAIL_N=$((FAIL_N+1)); printf '  \033[31mFAIL\033[0m %s (%s x%s)\n' "$name" "$hdr" "$n"
    fi
}

echo "  -- cdn_node_header"
check "return 200 gets headers"   $'^HTTP/1.1 200\n^X-Node: node-1$\n^X-Status: 200$\n^Cache-Control: public, max-age=60$\n!^X-Cond:' "$H/t"
check "map non-empty value sent"  $'^X-Cond: enabled$' "$H/t?on=1"
check "static file"               $'^X-Node: node-1$\n^Content-Type: text/plain\nhello' "$H/static/a.txt"
check "404 page"                  $'^HTTP/1.1 404\n^X-Node: node-1$\n^X-Status: 404$' "$H/static/missing"
check "403 from access phase"     $'^HTTP/1.1 403\n^X-Node: node-1$' "$H/locked/x"
check "error_page internal redir" $'^HTTP/1.1 200\n^X-Node: node-1$\nfallback' "$H/teapot"
check "HEAD request"              $'^X-Node: node-1$' -I "$H/t"
check "proxy + \$upstream_status"  $'^HTTP/1.1 202\n^X-Upstream: 202$\nbackend' "$H/proxy"
dup   "proxy: X-Node not dup"     X-Node "$H/proxy"
check "SSI subrequest body"       $'\\[sub\\]' "$H/page.html"
dup   "SSI: X-Node not dup"       X-Node "$H/page.html"

echo "  -- /cdn-cgi/trace (HTTP)"
check "trace body" $'^HTTP/1.1 200\n^Content-Type: text/plain\n^Cache-Control: no-store$\n^X-Content-Type-Options: nosniff$\n^X-Node: node-1$\n^h=localhost$\n^ip=127.0.0.1$\n^ts=[0-9]+\\.[0-9]{2}$\n^visit_scheme=http$\n^uag=smoke/1.0$\n^colo=HKG$\n^sliver=010-tier1$\n^http=HTTP/1.1$\n^tls=off$' \
      -A smoke/1.0 -H "Host: localhost" "$H/cdn-cgi/trace"
dup   "trace: Cache-Control once" Cache-Control "$H/cdn-cgi/trace"
check "trace HTTP/1.0"            $'^http=HTTP/1.0$' --http1.0 "$H/cdn-cgi/trace"
check "trace no UA"               $'^uag=$' -A "" "$H/cdn-cgi/trace"
check "trace query string"        $'^colo=HKG$' "$H/cdn-cgi/trace?x=1"
check "trace normalized uri"      $'^colo=HKG$' --path-as-is "$H//cdn-cgi/./x/../trace"
check "trace HEAD"                $'^HTTP/1.1 200\n^Cache-Control: no-store$\n!colo=' -I "$H/cdn-cgi/trace"
check "trace POST -> 405"         $'^HTTP/1.1 405\n!colo=' -X POST -d x "$H/cdn-cgi/trace"
check "trace/ -> not trace"       $'^HTTP/1.1 404\n!colo=' "$H/cdn-cgi/trace/"
check "case sensitive"            $'^HTTP/1.1 404\n!colo=' "$H/CDN-CGI/TRACE"
check "internal redirect no trace" $'static file\n!colo=' "$H/go-trace"

echo "  -- /cdn-cgi/trace (HTTPS / HTTP2)"
check "https h1 trace"  $'^visit_scheme=https$\n^http=HTTP/1.1$\n^tls=TLSv1\\.[23]$' --http1.1 "$S/cdn-cgi/trace"
check "https h2 trace"  $'^HTTP/2 200\n^x-node: node-1$\n^visit_scheme=https$\n^http=HTTP/2.0$\n^tls=TLSv1\\.[23]$' --http2 "$S/cdn-cgi/trace"
check "https h2 header" $'^HTTP/2 200\n^x-node: node-1$\n^x-status: 200$' --http2 "$S/t"

echo "  -- keepalive"
curl -sv "$H/cdn-cgi/trace" -o /dev/null "$H/t" -o "$SMOKE/body2" 2>"$SMOKE/ka.log" || true
if grep -q '^ok$' "$SMOKE/body2" && grep -qi 're-using existing\|reusing existing' "$SMOKE/ka.log"; then
    PASS_N=$((PASS_N+1)); printf '  \033[32mPASS\033[0m %s\n' "trace then /t on one connection"
else
    FAIL_N=$((FAIL_N+1)); printf '  \033[31mFAIL\033[0m %s\n' "trace then /t on one connection"
fi

if grep -E '\[(alert|crit|emerg)\]|cdn_node: failed' "$SMOKE/logs/error.log"; then
    FAIL_N=$((FAIL_N+1)); printf '  \033[31mFAIL\033[0m error.log has alerts\n'
fi

log "smoke: $PASS_N passed, $FAIL_N failed"
[ "$FAIL_N" = 0 ] || FAIL=1

if [ "$FAIL" = 0 ]; then
    log "ALL PASSED"
else
    die "some tests failed"
fi

#!/usr/bin/env bash
#
# 编译带本模块的 nginx 并运行 Test::Nginx 测试.
#
# 依赖: gcc make libpcre2-dev (或 libpcre3-dev) zlib1g-dev libssl-dev
#       cpanm Test::Nginx  (或 apt install libtest-nginx-perl)
#
# 用法:
#   util/test.sh                 # 编译 (如需要) 并运行全部测试
#   util/test.sh t/003-trace.t   # 只运行指定测试文件
#
# 环境变量:
#   NGINX_VERSION   nginx 版本, 默认 1.26.2
#   DYNAMIC=1       以动态模块方式编译 (--add-dynamic-module)
#   REBUILD=1       强制重新 configure + 编译

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NGINX_VERSION="${NGINX_VERSION:-1.26.2}"
BUILD_DIR="$ROOT/build"
SRC_DIR="$BUILD_DIR/nginx-$NGINX_VERSION"
PREFIX="$BUILD_DIR/nginx-$NGINX_VERSION-install${DYNAMIC:+-dynamic}"
NGINX_BIN="$PREFIX/sbin/nginx"

mkdir -p "$BUILD_DIR"

if [ ! -d "$SRC_DIR" ]; then
    echo ">>> downloading nginx-$NGINX_VERSION"
    curl -fsSL "https://nginx.org/download/nginx-$NGINX_VERSION.tar.gz" \
        | tar -xz -C "$BUILD_DIR"
fi

if [ "${REBUILD:-0}" = 1 ] || [ ! -x "$NGINX_BIN" ] \
   || [ -n "$(find "$ROOT/src" "$ROOT/config" -newer "$NGINX_BIN" -print -quit)" ]
then
    if [ -n "${DYNAMIC:-}" ]; then
        MODULE_OPT="--add-dynamic-module=$ROOT"
    else
        MODULE_OPT="--add-module=$ROOT"
    fi

    echo ">>> building nginx-$NGINX_VERSION with ngx_http_cdn_node_module"
    (
        cd "$SRC_DIR"
        ./configure \
            --prefix="$PREFIX" \
            --with-debug \
            --with-http_ssl_module \
            --with-http_v2_module \
            --with-cc-opt="-O0 -g -Werror" \
            "$MODULE_OPT" \
            > "$BUILD_DIR/configure.log"
        make -j"$(nproc 2>/dev/null || echo 2)" > "$BUILD_DIR/make.log"
        make install > /dev/null
    )
fi

"$NGINX_BIN" -V 2>&1 | head -1

export PATH="$PREFIX/sbin:$PATH"

if [ -n "${DYNAMIC:-}" ]; then
    export TEST_NGINX_LOAD_MODULES="$PREFIX/modules/ngx_http_cdn_node_module.so"
fi

cd "$ROOT"

if [ "$#" -eq 0 ]; then
    set -- t/
fi

exec prove -r "$@"


/*
 * ngx_http_cdn_node_module
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>


/* NGX_HTTP_PRECONTENT_PHASE 在 nginx 1.13.4 中引入 */
#if !defined(nginx_version) || nginx_version < 1013004
#error "ngx_http_cdn_node_module requires nginx 1.13.4 or later (NGX_HTTP_PRECONTENT_PHASE)"
#endif


/* 单个 header 配置 */
typedef struct {
    ngx_str_t                  header_name;
    ngx_http_complex_value_t   header_value;
} ngx_http_cdn_node_header_entry_conf_t;


/* 模块配置 (仅 http 级) */
typedef struct {
    ngx_array_t               *headers;
    ngx_str_t                  colo;
    ngx_str_t                  sliver;
} ngx_http_cdn_node_conf_t;


static ngx_uint_t ngx_http_cdn_node_is_tchar(u_char c);
static ngx_int_t ngx_http_cdn_node_add_header(ngx_http_request_t *r,
    ngx_str_t *key, ngx_str_t *value);
static ngx_int_t ngx_http_cdn_node_trace_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_cdn_node_trace_phase_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_cdn_node_header_filter(ngx_http_request_t *r);

static void *ngx_http_cdn_node_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_cdn_node_init_main_conf(ngx_conf_t *cf, void *conf);
static char *ngx_http_cdn_node_header_config(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_cdn_node_init(ngx_conf_t *cf);


static ngx_command_t  ngx_http_cdn_node_commands[] = {

    { ngx_string("cdn_node_header"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_cdn_node_header_config,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("cdn_node_colo"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_cdn_node_conf_t, colo),
      NULL },

    { ngx_string("cdn_node_sliver"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_cdn_node_conf_t, sliver),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_cdn_node_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_cdn_node_init,                /* postconfiguration */

    ngx_http_cdn_node_create_main_conf,    /* create main configuration */
    ngx_http_cdn_node_init_main_conf,      /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_cdn_node_module = {
    NGX_MODULE_V1,
    &ngx_http_cdn_node_module_ctx,         /* module context */
    ngx_http_cdn_node_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;


/*
 * 由 ngx_http_header_filter 根据 headers_out 中的专用字段生成的 header.
 * 追加到 headers 列表只会产生重复项 (Content-Length/Transfer-Encoding
 * 重复还会导致响应解析歧义), 所以禁止配置
 */
static ngx_str_t  ngx_http_cdn_node_reserved_headers[] = {
    ngx_string("Server"),
    ngx_string("Date"),
    ngx_string("Content-Type"),
    ngx_string("Content-Length"),
    ngx_string("Transfer-Encoding"),
    ngx_string("Connection"),
    ngx_string("Keep-Alive"),
    ngx_string("Last-Modified"),
    ngx_null_string
};


/* trace 响应中除字段值以外的固定文本, 用于计算缓冲区大小 */
#define NGX_HTTP_CDN_NODE_TRACE_FIXED_TEXT                                    \
    "h=\nip=\nts=.\nvisit_scheme=\nuag=\ncolo=\nsliver=\nhttp=\ntls=\n"


static ngx_str_t  ngx_http_cdn_node_cache_control =
    ngx_string("Cache-Control");
static ngx_str_t  ngx_http_cdn_node_no_store = ngx_string("no-store");
static ngx_str_t  ngx_http_cdn_node_content_type_options =
    ngx_string("X-Content-Type-Options");
static ngx_str_t  ngx_http_cdn_node_nosniff = ngx_string("nosniff");


/* 判断是否为 RFC 9110 中的 tchar */

static ngx_uint_t
ngx_http_cdn_node_is_tchar(u_char c)
{
    if ((c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9'))
    {
        return 1;
    }

    return ngx_strchr("!#$%&'*+-.^_`|~", c) != NULL;
}


/* 向响应追加一个 header */

static ngx_int_t
ngx_http_cdn_node_add_header(ngx_http_request_t *r, ngx_str_t *key,
    ngx_str_t *value)
{
    ngx_table_elt_t  *h;

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    h->hash = 1;
    h->key = *key;
    h->value = *value;
    h->next = NULL;

    return NGX_OK;
}


/* 生成 /cdn-cgi/trace 响应内容 */

static ngx_int_t
ngx_http_cdn_node_trace_handler(ngx_http_request_t *r)
{
    size_t                     size, len, tls_len;
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_str_t                  scheme, ua;
    const char                *tls;
    ngx_chain_t                out;
    struct timespec            ts;
    ngx_http_cdn_node_conf_t  *cncf;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    cncf = ngx_http_get_module_main_conf(r, ngx_http_cdn_node_module);

    clock_gettime(CLOCK_REALTIME, &ts);

    ngx_str_set(&scheme, "http");
#if (NGX_HTTP_SSL)
    if (r->connection->ssl) {
        ngx_str_set(&scheme, "https");
    }
#endif

    /* 客户端可能不发送 User-Agent */
    if (r->headers_in.user_agent) {
        ua = r->headers_in.user_agent->value;

    } else {
        ngx_str_set(&ua, "");
    }

    tls = "off";
#if (NGX_HTTP_SSL)
    if (r->connection->ssl) {
        tls = SSL_get_version(r->connection->ssl->connection);
    }
#endif
    tls_len = ngx_strlen(tls);

    /*
     * 按实际字段长度从请求 pool 分配, 不再使用固定缓冲区,
     * 超长 Host/UA 也能完整输出.
     * 各字段长度都有上限 (请求头受 large_client_header_buffers 限制),
     * 所以总大小有界.
     * 秒数占 NGX_TIME_T_LEN, 小数部分固定 2 位
     */
    size = sizeof(NGX_HTTP_CDN_NODE_TRACE_FIXED_TEXT) - 1
           + r->headers_in.server.len
           + r->connection->addr_text.len
           + NGX_TIME_T_LEN + 2
           + scheme.len
           + ua.len
           + cncf->colo.len
           + cncf->sliver.len
           + r->http_protocol.len
           + tls_len;

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* ngx_slprintf 以 b->end 为边界, 即使长度估算有误也只会截断而不会越界 */
    b->last = ngx_slprintf(b->last, b->end,
                           "h=%V\n"
                           "ip=%V\n"
                           "ts=%T.%02l\n"
                           "visit_scheme=%V\n"
                           "uag=%V\n"
                           "colo=%V\n"
                           "sliver=%V\n"
                           "http=%V\n"
                           "tls=%s\n",
                           &r->headers_in.server,
                           &r->connection->addr_text,
                           (time_t) ts.tv_sec, (long) (ts.tv_nsec / 10000000),
                           &scheme,
                           &ua,
                           &cncf->colo,
                           &cncf->sliver,
                           &r->http_protocol,
                           tls);

    len = b->last - b->pos;
    b->last_buf = 1;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = len;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    r->headers_out.content_type_len = r->headers_out.content_type.len;

    /*
     * trace 回显客户端 IP/UA/Host:
     * no-store 防止中间缓存把某个客户端的结果返回给其他人,
     * nosniff 防止浏览器把回显内容当作 HTML 解析 (反射型 XSS)
     */
    if (ngx_http_cdn_node_add_header(r, &ngx_http_cdn_node_cache_control,
                                     &ngx_http_cdn_node_no_store)
        != NGX_OK
        || ngx_http_cdn_node_add_header(r,
                                     &ngx_http_cdn_node_content_type_options,
                                     &ngx_http_cdn_node_nosniff)
           != NGX_OK)
    {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * 本模块的响应即最终结果: 自行 finalize 并返回 NGX_DONE,
     * 阻止 phase 引擎继续进入 CONTENT 阶段 (proxy_pass/static 等)
     */
    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        ngx_http_finalize_request(r, rc);
        return NGX_DONE;
    }

    ngx_http_finalize_request(r, ngx_http_output_filter(r, &out));

    return NGX_DONE;
}


/* /cdn-cgi/trace handler, 挂在 PRECONTENT 阶段, 在 access 阶段之后执行 */

static ngx_int_t
ngx_http_cdn_node_trace_phase_handler(ngx_http_request_t *r)
{
    ngx_http_cdn_node_conf_t  *cncf;

    if (r->main->internal) {
        return NGX_DECLINED;
    }

    cncf = ngx_http_get_module_main_conf(r, ngx_http_cdn_node_module);

    if (cncf->headers->nelts == 0) {
        return NGX_DECLINED;
    }

    if (r->uri.len == sizeof("/cdn-cgi/trace") - 1
        && ngx_strncmp(r->uri.data, "/cdn-cgi/trace",
                       sizeof("/cdn-cgi/trace") - 1)
           == 0)
    {
        return ngx_http_cdn_node_trace_handler(r);
    }

    return NGX_DECLINED;
}


/*
 * 响应头过滤器: 在发送响应头时追加配置的 header.
 * 所有响应 (包括 return/错误页/内部跳转后的响应) 都会经过这里,
 * 且此时可取到 $status/$upstream_* 等响应阶段变量
 */

static ngx_int_t
ngx_http_cdn_node_header_filter(ngx_http_request_t *r)
{
    ngx_str_t                               value;
    ngx_uint_t                              i;
    ngx_table_elt_t                        *h;
    ngx_http_cdn_node_conf_t               *cncf;
    ngx_http_cdn_node_header_entry_conf_t  *header;

    /* 子请求的响应头不会发给客户端, 跳过 */
    if (r != r->main) {
        return ngx_http_next_header_filter(r);
    }

    cncf = ngx_http_get_module_main_conf(r, ngx_http_cdn_node_module);

    if (cncf == NULL || cncf->headers->nelts == 0) {
        return ngx_http_next_header_filter(r);
    }

    header = cncf->headers->elts;

    for (i = 0; i < cncf->headers->nelts; i++) {

        /* header filter 中返回错误会直接断开连接, 求值失败时只记日志并跳过 */
        if (ngx_http_complex_value(r, &header[i].header_value, &value)
            != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "cdn_node: failed to evaluate header \"%V\"",
                          &header[i].header_name);
            continue;
        }

        /* 与 add_header 一致: 值为空时不输出, 便于配合 map 按条件添加 */
        if (value.len == 0) {
            continue;
        }

        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }

        h->hash = 1;
        h->key = header[i].header_name;
        h->value = value;
        h->next = NULL;
    }

    return ngx_http_next_header_filter(r);
}


static void *
ngx_http_cdn_node_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_cdn_node_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_cdn_node_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->headers = ngx_array_create(cf->pool, 4,
                                sizeof(ngx_http_cdn_node_header_entry_conf_t));
    if (conf->headers == NULL) {
        return NULL;
    }

    return conf;
}


/* 配置解析完成后, 为未设置的值填默认值 */

static char *
ngx_http_cdn_node_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_cdn_node_conf_t *cncf = conf;

    if (cncf->colo.data == NULL) {
        ngx_str_set(&cncf->colo, "");
    }

    if (cncf->sliver.data == NULL) {
        ngx_str_set(&cncf->sliver, "");
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_cdn_node_header_config(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_cdn_node_conf_t *cncf = conf;

    size_t                                  i;
    ngx_str_t                              *value, *reserved;
    ngx_http_compile_complex_value_t        ccv;
    ngx_http_cdn_node_header_entry_conf_t  *header;

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "empty header name in \"%V\"", &cmd->name);
        return NGX_CONF_ERROR;
    }

    for (i = 0; i < value[1].len; i++) {
        if (!ngx_http_cdn_node_is_tchar(value[1].data[i])) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid header name \"%V\" in \"%V\"",
                               &value[1], &cmd->name);
            return NGX_CONF_ERROR;
        }
    }

    for (reserved = ngx_http_cdn_node_reserved_headers;
         reserved->len;
         reserved++)
    {
        if (reserved->len == value[1].len
            && ngx_strncasecmp(reserved->data, value[1].data, value[1].len)
               == 0)
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "header \"%V\" is generated by nginx "
                               "and cannot be set by \"%V\"",
                               &value[1], &cmd->name);
            return NGX_CONF_ERROR;
        }
    }

    header = ngx_array_push(cncf->headers);
    if (header == NULL) {
        return NGX_CONF_ERROR;
    }

    header->header_name = value[1];

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[2];
    ccv.complex_value = &header->header_value;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_cdn_node_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_cdn_node_trace_phase_handler;

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_cdn_node_header_filter;

    return NGX_OK;
}

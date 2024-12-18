#include "ngx_http_cdn_node_module.h"

#include <ngx_config.h>


// 模块指令
static ngx_command_t ngx_http_cdn_node_commands[] = {
        {
                ngx_string("cdn_node_header"),
                NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
                ngx_http_cdn_node_header_config,
                NGX_HTTP_LOC_CONF_OFFSET,
                0,
                NULL
        },
        {
                ngx_string("cdn_node_colo"),
                NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
                ngx_conf_set_str_slot,
                NGX_HTTP_LOC_CONF_OFFSET,
                offsetof(ngx_http_cdn_node_conf_t, colo),
                NULL
        },
        {
                ngx_string("cdn_node_sliver"),
                NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
                ngx_conf_set_str_slot,
                NGX_HTTP_LOC_CONF_OFFSET,
                offsetof(ngx_http_cdn_node_conf_t, sliver),
                NULL
        },
        ngx_null_command
};

// 模块上下文
static ngx_http_module_t ngx_http_cdn_node_module_ctx = {
        NULL,                               /* preconfiguration */
        ngx_http_cdn_node_init,            /* postconfiguration */
        NULL,                              /* create main configuration */
        NULL,                              /* init main configuration */
        NULL,                              /* create server configuration */
        NULL,                              /* merge server configuration */
        ngx_http_cdn_node_create_loc_conf, /* create location configuration */
        ngx_http_cdn_node_merge_loc_conf   /* merge location configuration */
};

// 模块定义
ngx_module_t ngx_http_cdn_node_module = {
        NGX_MODULE_V1,
        &ngx_http_cdn_node_module_ctx,     /* module context */
        ngx_http_cdn_node_commands,        /* module directives */
        NGX_HTTP_MODULE,                   /* module type */
        NULL,                              /* init master */
        NULL,                              /* init module */
        NULL,                              /* init process */
        NULL,                              /* init thread */
        NULL,                              /* exit thread */
        NULL,                              /* exit process */
        NULL,                              /* exit master */
        NGX_MODULE_V1_PADDING
};

// 创建配置
static void *ngx_http_cdn_node_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_cdn_node_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_cdn_node_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->headers = ngx_array_create(cf->pool, 4, sizeof(ngx_http_cdn_node_header_entry_conf_t));
    if (conf->headers == NULL) {
        return NULL;
    }

    return conf;
}

// 合并配置
static char *ngx_http_cdn_node_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
    ngx_http_cdn_node_conf_t *prev = parent;
    ngx_http_cdn_node_conf_t *conf = child;

    if (conf->headers->nelts == 0) {
        conf->headers = prev->headers;
    }

    ngx_conf_merge_str_value(conf->colo, prev->colo, "");
    ngx_conf_merge_str_value(conf->sliver, prev->sliver, "");

    return NGX_CONF_OK;
}

// header指令处理
static char *ngx_http_cdn_node_header_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_cdn_node_conf_t *config = conf;
    ngx_str_t *args = cf->args->elts;
    ngx_http_cdn_node_header_entry_conf_t *header;
    ngx_http_compile_complex_value_t ccv;

    header = ngx_array_push(config->headers);
    if (header == NULL) {
        return NGX_CONF_ERROR;
    }

    header->header_name = args[1];

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
    ccv.cf = cf;
    ccv.value = &args[2];
    ccv.complex_value = &header->header_value;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

// 生成/cdn-cgi/trace响应内容
static ngx_int_t ngx_http_cdn_node_trace_handler(ngx_http_request_t *r) {
    ngx_http_cdn_node_conf_t *config;
    ngx_buf_t *b;
    ngx_chain_t out;
    struct timespec ts;
    char buffer[1024];
    int len;
    ngx_str_t scheme;

    config = ngx_http_get_module_loc_conf(r, ngx_http_cdn_node_module);

    clock_gettime(CLOCK_REALTIME, &ts);



#if (NGX_HTTP_SSL)
    if (r->connection->ssl) {
        ngx_str_set(&scheme, "https");
    } else {
        ngx_str_set(&scheme, "http");
    }
#endif

    len = snprintf(buffer, sizeof(buffer),
                   "h=%.*s\n"
                   "ip=%.*s\n"
                   "ts=%ld.%02ld\n"
                   "visit_scheme=%.*s\n"
                   "uag=%.*s\n"
                   "colo=%.*s\n"
                   "sliver=%.*s\n"
                   "http=%.*s\n"
                   "tls=%s\n",
                   (int)r->headers_in.server.len, r->headers_in.server.data,
                   (int)r->connection->addr_text.len, r->connection->addr_text.data,
                   ts.tv_sec, ts.tv_nsec / 10000000,
                   (int)scheme.len, scheme.data,
                   (int)r->headers_in.user_agent->value.len, r->headers_in.user_agent->value.data,
                   (int)config->colo.len, config->colo.data,
                   (int)config->sliver.len, config->sliver.data,
                   (int)r->http_protocol.len, r->http_protocol.data,

#if (NGX_HTTP_SSL)
                   r->connection->ssl ? SSL_get_version(r->connection->ssl->connection) : "off"
#else
            "off"
#endif
    );

    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->last = ngx_cpymem(b->last, buffer, len);
    b->last_buf = 1;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = len;
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";

    ngx_http_send_header(r);
    return ngx_http_output_filter(r, &out);
}

// 请求处理handler
static ngx_int_t ngx_http_cdn_node_handler(ngx_http_request_t *r) {
    ngx_http_cdn_node_conf_t *config;
    ngx_http_cdn_node_header_entry_conf_t *header;
    ngx_uint_t i;
    ngx_str_t value;
    ngx_table_elt_t *h;

    if (r->main->internal) {
        return NGX_DECLINED;
    }

    config = ngx_http_get_module_loc_conf(r, ngx_http_cdn_node_module);
    if (config->headers->nelts == 0) {
        return NGX_DECLINED;
    }

    header = config->headers->elts;
    for (i = 0; i < config->headers->nelts; i++) {
        if (ngx_http_complex_value(r, &header[i].header_value, &value) != NGX_OK) {
            return NGX_ERROR;
        }

        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }

        h->hash = 1;
        h->key = header[i].header_name;
        h->value = value;
    }

    // 检查是否是/cdn-cgi/trace请求
    if (r->uri.len == sizeof("/cdn-cgi/trace") - 1 &&
        ngx_strncmp(r->uri.data, "/cdn-cgi/trace", sizeof("/cdn-cgi/trace") - 1) == 0) {
        return ngx_http_cdn_node_trace_handler(r);
    }

    return NGX_DECLINED;
}

// 模块初始化
static ngx_int_t ngx_http_cdn_node_init(ngx_conf_t *cf) {
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_cdn_node_handler;

    return NGX_OK;
}

#include "ngx_http_cdn_node_module.h"

#include <ngx_config.h>


// 模块指令
static ngx_command_t ngx_http_cdn_node_commands[] = {
        {
                ngx_string("cdn_node_header"),
                NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
                ngx_http_cdn_node_header,
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
        NULL,                               // preconfiguration
        ngx_http_cdn_node_init,            // postconfiguration
        NULL,                              // create main configuration
        NULL,                              // init main configuration
        NULL,                              // create server configuration
        NULL,                              // merge server configuration
        ngx_http_cdn_node_create_loc_conf, // create location configuration
        ngx_http_cdn_node_merge_loc_conf   // merge location configuration
};

// 模块定义
ngx_module_t ngx_http_cdn_node_module = {
        NGX_MODULE_V1,
        &ngx_http_cdn_node_module_ctx,     // module context
        ngx_http_cdn_node_commands,        // module directives
        NGX_HTTP_MODULE,                   // module type
        NULL,                              // init master
        NULL,                              // init module
        NULL,                              // init process
        NULL,                              // init thread
        NULL,                              // exit thread
        NULL,                              // exit process
        NULL,                              // exit master
        NGX_MODULE_V1_PADDING
};

// header过滤器处理函数
static ngx_http_output_header_filter_pt ngx_http_next_header_filter;

    static ngx_int_t ngx_http_cdn_node_header_filter(ngx_http_request_t *r)
{
    ngx_http_cdn_node_conf_t *conf;
    ngx_http_cdn_node_header_entry_conf_t *header;
    ngx_table_elt_t *h;
    ngx_uint_t i;
    ngx_str_t value;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_cdn_node_module);

    if (conf->headers == NULL || conf->headers->nelts == 0) {
        return ngx_http_next_header_filter(r);
    }

    header = conf->headers->elts;

    for (i = 0; i < conf->headers->nelts; i++) {
        if (ngx_http_complex_value(r, &header[i].header_value, &value) != NGX_OK) {
            return NGX_ERROR;
        }

        if (header[i].header_name.len > 0 && value.len > 0) {
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                return NGX_ERROR;
            }

            h->hash = 1;
            h->key = header[i].header_name;
            h->value = value;
        }
    }

    return ngx_http_next_header_filter(r);
}

// 创建配置
static void *ngx_http_cdn_node_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_cdn_node_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_cdn_node_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->headers = ngx_array_create(cf->pool, 4, sizeof(ngx_http_cdn_node_header_entry_conf_t));
    if (conf->headers == NULL) {
        return NULL;
    }

    ngx_str_null(&conf->colo);
    ngx_str_null(&conf->sliver);

    return conf;
}

// 合并配置
static char *ngx_http_cdn_node_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_cdn_node_conf_t *prev = parent;
    ngx_http_cdn_node_conf_t *conf = child;
    ngx_http_cdn_node_header_entry_conf_t *header;
    ngx_uint_t i;

    if (conf->headers == NULL) {
        conf->headers = prev->headers;
    } else if (prev->headers != NULL) {
        header = prev->headers->elts;
        for (i = 0; i < prev->headers->nelts; i++) {
            ngx_http_cdn_node_header_entry_conf_t *new_header;
            new_header = ngx_array_push(conf->headers);
            if (new_header == NULL) {
                return NGX_CONF_ERROR;
            }
            *new_header = header[i];
        }
    }

    ngx_conf_merge_str_value(conf->colo, prev->colo, "");
    ngx_conf_merge_str_value(conf->sliver, prev->sliver, "");

    return NGX_CONF_OK;
}

// 自定义配置处理函数
static char *ngx_http_cdn_node_header(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_cdn_node_conf_t *cnhc = conf;
    ngx_http_cdn_node_header_entry_conf_t *header;
    ngx_str_t *args;
    ngx_http_compile_complex_value_t ccv;

    args = cf->args->elts;

    header = ngx_array_push(cnhc->headers);
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


static ngx_int_t ngx_http_cdn_node_cdn_cgi_trace_handler(ngx_http_request_t *r) {
    ngx_http_cdn_node_conf_t *conf;
    ngx_str_t uri = ngx_string("/cdn-cgi/trace");
    ngx_buf_t *b;
    ngx_chain_t out;
    size_t size;
    u_char *p;
    struct timeval tv;
    ngx_str_t scheme;

    if (r->uri.len != uri.len || ngx_strncmp(r->uri.data, uri.data, uri.len) != 0) {
        return NGX_DECLINED;
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_cdn_node_module);

    // 获取协议
    if (r->connection->ssl) {
        ngx_str_set(&scheme, "https");
    } else {
        ngx_str_set(&scheme, "http");
    }

    size = sizeof("h=") - 1 + (r->headers_in.server.len ? r->headers_in.server.len : 1) + 1
           + sizeof("ip=") - 1 + r->connection->addr_text.len + 1
           + sizeof("ts=") - 1 + 32 + 1
           + sizeof("visit_scheme=") - 1 + scheme.len + 1
           + sizeof("uag=") - 1 + (r->headers_in.user_agent ? r->headers_in.user_agent->value.len : 1) + 1
           + sizeof("colo=") - 1 + conf->colo.len + 1
           + sizeof("sliver=") - 1 + conf->sliver.len + 1
           + sizeof("http=") - 1 + r->http_protocol.len + 1
           + sizeof("tls=off\n") - 1;

    // 分配缓冲区
    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    // 构建响应内容
    p = b->pos;

    // 主机名
    if (r->headers_in.server.len) {
        p = ngx_sprintf(p, "h=%V\n", &r->headers_in.server);
    } else {
        p = ngx_sprintf(p, "h=unknown\n");
    }

    // 客户端IP
    p = ngx_sprintf(p, "ip=%V\n", &r->connection->addr_text);

    // UNIX时间戳（带小数点后两位）
    ngx_gettimeofday(&tv);
    p = ngx_sprintf(p, "ts=%ui.%02ui\n",
                    (ngx_uint_t)tv.tv_sec,
                    (ngx_uint_t)(tv.tv_usec / 10000));

    // 访问协议
    p = ngx_sprintf(p, "visit_scheme=%V\n", &scheme);

    // User Agent
    if (r->headers_in.user_agent) {
        p = ngx_sprintf(p, "uag=%V\n", &r->headers_in.user_agent->value);
    } else {
        p = ngx_sprintf(p, "uag=-\n");
    }

    // COLO和SLIVER配置值
    p = ngx_sprintf(p, "colo=%V\n", &conf->colo);
    p = ngx_sprintf(p, "sliver=%V\n", &conf->sliver);

    // HTTP版本
    p = ngx_sprintf(p, "http=%V\n", &r->http_protocol);

    // SSL相关信息
    if (r->connection->ssl) {
#if (NGX_SSL)
        SSL *ssl = r->connection->ssl->connection;
        p = ngx_sprintf(p, "tls=%s\n", SSL_get_version(ssl));
#endif
    } else {
        p = ngx_sprintf(p, "tls=off\n");
    }

    b->last = p;
    b->last_buf = 1;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";

    // 禁用缓存
    r->headers_out.expires = NULL;

    ngx_http_send_header(r);

    return ngx_http_output_filter(r, &out);
}

// 初始化模块
static ngx_int_t ngx_http_cdn_node_init(ngx_conf_t *cf)
{
        //处理header过滤器
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_cdn_node_header_filter;

    //处理HTTP请求
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_cdn_node_cdn_cgi_trace_handler;



    return NGX_OK;
}
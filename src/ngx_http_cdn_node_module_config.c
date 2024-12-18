#include "ngx_http_cdn_node_module_config.h"

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
    ngx_http_cdn_node_header_array_conf_t *conf;
    ngx_http_cdn_node_header_entry_conf_t *header;
    ngx_table_elt_t *h;
    ngx_uint_t i;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_cdn_node_module);

    if (conf->headers == NULL || conf->headers->nelts == 0) {
        return ngx_http_next_header_filter(r);
    }

    header = conf->headers->elts;
    for (i = 0; i < conf->headers->nelts; i++) {
        if (header[i].header_name.len > 0 && header[i].header_value.len > 0) {
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                return NGX_ERROR;
            }

            h->hash = 1;
            h->key = header[i].header_name;
            h->value = header[i].header_value;
        }
    }

    return ngx_http_next_header_filter(r);
}

// 创建配置
static void *ngx_http_cdn_node_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_cdn_node_header_array_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_cdn_node_header_array_conf_t));
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
static char *ngx_http_cdn_node_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_cdn_node_header_array_conf_t *prev = parent;
    ngx_http_cdn_node_header_array_conf_t *conf = child;
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

    return NGX_CONF_OK;
}

// 自定义配置处理函数
static char *ngx_http_cdn_node_header(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_cdn_node_header_array_conf_t *cnhc = conf;
    ngx_http_cdn_node_header_entry_conf_t *header;
    ngx_str_t *args;

    args = cf->args->elts;

    header = ngx_array_push(cnhc->headers);
    if (header == NULL) {
        return NGX_CONF_ERROR;
    }

    header->header_name = args[1];
    header->header_value = args[2];

    return NGX_CONF_OK;
}

// 初始化模块
static ngx_int_t ngx_http_cdn_node_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_cdn_node_header_filter;

    return NGX_OK;
}
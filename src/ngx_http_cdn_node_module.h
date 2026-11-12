#ifndef NGX_HTTP_CDN_NODE_MODULE_CONFIG_H
#define NGX_HTTP_CDN_NODE_MODULE_CONFIG_H


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

// NGX_HTTP_PRECONTENT_PHASE 在 nginx 1.13.4 中引入
#if !defined(nginx_version) || nginx_version < 1013004
#error "ngx_http_cdn_node_module requires nginx 1.13.4 or later (NGX_HTTP_PRECONTENT_PHASE)"
#endif

// 单个header配置结构体
typedef struct {
    ngx_str_t header_name;
    ngx_http_complex_value_t  header_value;
} ngx_http_cdn_node_header_entry_conf_t;

// 模块配置结构体 (仅http级)
typedef struct {
    ngx_array_t *headers;
    ngx_str_t colo;
    ngx_str_t sliver;
} ngx_http_cdn_node_conf_t;



// 函数声明
static void *ngx_http_cdn_node_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_cdn_node_init_main_conf(ngx_conf_t *cf, void *conf);
static ngx_int_t ngx_http_cdn_node_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_cdn_node_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_cdn_node_trace_phase_handler(ngx_http_request_t *r);
static char *ngx_http_cdn_node_header_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);



#endif //NGX_HTTP_CDN_NODE_MODULE_CONFIG_H

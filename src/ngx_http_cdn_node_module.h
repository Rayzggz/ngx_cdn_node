#ifndef NGX_HTTP_CDN_NODE_MODULE_CONFIG_H
#define NGX_HTTP_CDN_NODE_MODULE_CONFIG_H


#include <ngx_core.h>
#include <ngx_http.h>

// 单个header配置结构体
typedef struct {
    ngx_str_t header_name;
    ngx_http_complex_value_t  header_value;
} ngx_http_cdn_node_header_entry_conf_t;

// 模块配置结构体
typedef struct {
    ngx_array_t *headers;
    ngx_str_t colo;
    ngx_str_t sliver;
} ngx_http_cdn_node_conf_t;



// 函数声明
static void *ngx_http_cdn_node_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_cdn_node_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_cdn_node_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_cdn_node_header_filter(ngx_http_request_t *r);
static char *ngx_http_cdn_node_header(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_cdn_node_cdn_cgi_trace_handler(ngx_http_request_t *r);


#endif //NGX_HTTP_CDN_NODE_MODULE_CONFIG_H
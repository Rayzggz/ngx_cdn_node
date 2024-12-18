#ifndef NGX_HTTP_CDN_NODE_MODULE_CONFIG_H
#define NGX_HTTP_CDN_NODE_MODULE_CONFIG_H


#include <ngx_core.h>
#include <ngx_http.h>

// 模块配置结构体
typedef struct {
    ngx_str_t header_name;  // 要添加的 header 名称
    ngx_str_t header_value; // 要添加的 header 值
} ngx_http_cdn_node_header_conf_t;

// 函数声明
static void *ngx_http_cdn_node_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_cdn_node_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_cdn_node_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_cdn_node_header_filter(ngx_http_request_t *r);
static char *ngx_http_cdn_node_header(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


#endif //NGX_HTTP_CDN_NODE_MODULE_CONFIG_H
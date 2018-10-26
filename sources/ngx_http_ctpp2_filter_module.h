
/*
 * Copyright (C) Valentin V. Bartenev
 */


#ifndef _NGX_HTTP_CTPP2_FILTER_H_INCLUDED_
#define _NGX_HTTP_CTPP2_FILTER_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
	ngx_buf_t           *data;
	
	ngx_buf_t           *tmpl;
	ngx_str_t            tmpl_path;
	unsigned             template_ready:1;
} ngx_http_ctpp2_ctx_t;


extern ngx_module_t  ngx_http_ctpp2_filter_module;


#endif /* _NGX_HTTP_CTPP2_FILTER_H_INCLUDED_ */

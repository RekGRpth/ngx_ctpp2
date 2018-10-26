
/*
 * Copyright (C) Valentin V. Bartenev
 */


#include "ngx_http_ctpp2_filter_module.h"


static ngx_int_t ngx_http_ctpp2_tmpl_loader_filter(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_ctpp2_tmpl_loader_init(ngx_conf_t *cf);

static ngx_http_output_body_filter_pt    ngx_http_next_filter;


static ngx_http_module_t  ngx_http_ctpp2_tmpl_loader_ctx = {
	NULL,                                  /* preconfiguration */
	ngx_http_ctpp2_tmpl_loader_init,       /* postconfiguration */

	NULL,                                  /* create main configuration */
	NULL,                                  /* init main configuration */

	NULL,                                  /* create server configuration */
	NULL,                                  /* merge server configuration */

	NULL,                                  /* create location configuration */
	NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_ctpp2_tmpl_loader = {
	NGX_MODULE_V1,
	&ngx_http_ctpp2_tmpl_loader_ctx,       /* module context */
	NULL,                                  /* module directives */
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


static ngx_int_t
ngx_http_ctpp2_tmpl_loader_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
	ngx_http_ctpp2_ctx_t      *ctx;
	ngx_str_t                 *path;
	ngx_log_t                 *log;
	ngx_open_file_info_t       of;
	ngx_http_core_loc_conf_t  *clcf;
	ngx_buf_t                 *b;
	ngx_chain_t                out;
	
	if (in == NULL) {
		return ngx_http_next_filter(r, in);
	}
	
	ctx = ngx_http_get_module_ctx(r, ngx_http_ctpp2_filter_module);
	if (ctx == NULL || ctx->tmpl) {
		return ngx_http_next_filter(r, in);
	}

	clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
	log = r->connection->log;

	ngx_memzero(&of, sizeof(ngx_open_file_info_t));
	of.read_ahead = clcf->read_ahead;
	of.directio = clcf->directio;
	of.valid = clcf->open_file_cache_valid;
	of.min_uses = clcf->open_file_cache_min_uses;
	of.errors = clcf->open_file_cache_errors;
	of.events = clcf->open_file_cache_events;
	
	path = &ctx->tmpl_path;
	
	if (ngx_open_cached_file(clcf->open_file_cache, path, &of, r->pool) != NGX_OK) {
		if (of.err) {
			ngx_log_error(NGX_LOG_ERR, log, 0,
				"Opening template file \"%s\" failed", path->data);
		} else {
			ngx_log_error(NGX_LOG_ERR, log, 0,
				"Opening template file error. %s \"%s\" failed", of.failed, path->data);
		}
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	if (!of.is_file) {
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"Template \"%s\" is not a regular file", path->data);
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	if (!of.size) {
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"Template \"%s\" has zero size", path->data);
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0,
		"http ctpp2 template loader: Allocating %d bytes for template buffer", of.size);
	ctx->tmpl = ngx_create_temp_buf(r->pool, of.size);
	if (ctx->tmpl == NULL) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if (b == NULL) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	b->file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));
	if (b->file == NULL) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	b->file_last = of.size;

	b->in_file = 1;

	b->file->fd = of.fd;
	b->file->name = *path;
	b->file->log = log;
	b->file->directio = of.is_directio;
	
	out.buf = b;
	out.next = in;
	
	return ngx_http_next_filter(r, &out);
}


static ngx_int_t
ngx_http_ctpp2_tmpl_loader_init(ngx_conf_t *cf)
{
	ngx_http_next_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_http_ctpp2_tmpl_loader_filter;

	return NGX_OK;
}

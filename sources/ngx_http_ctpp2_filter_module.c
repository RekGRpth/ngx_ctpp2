
/*
 * Copyright (C) Valentin V. Bartenev
 */


#include "ngx_http_ctpp2_filter_module.h"
#include "ctpp2_process.h"

#define NGX_HTTP_CTPP2_BUFFERED  0x80
#define NGX_HTTP_CTPP2_TMPLS_HEADER  "x-template"


typedef struct {
	ngx_uint_t  args;
	ngx_uint_t  code;
	ngx_uint_t  funcs;
	ngx_uint_t  steps;
} ngx_http_ctpp2_main_conf_t;

typedef struct {
	ngx_flag_t  enable;
	size_t      buffer_size;
	ngx_str_t   tmpls_header;
	ngx_flag_t  tmpls_check;
	ngx_http_complex_value_t  *tmpl;
	ngx_http_complex_value_t  *tmpls_root;
	ngx_buf_t  *tmpl_cache;
} ngx_http_ctpp2_loc_conf_t;

static ngx_int_t ngx_http_ctpp2_header_filter(ngx_http_request_t *r);
static ngx_str_t *ngx_http_ctpp2_get_tmpl_header(ngx_http_request_t *r, ngx_str_t *name);

static ngx_int_t ngx_http_ctpp2_body_filter(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_ctpp2_fillbuffer(ngx_buf_t *buf, ngx_chain_t **in);

static void *ngx_http_ctpp2_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_ctpp2_init_main_conf(ngx_conf_t *cf, void *conf);

static char *ngx_http_set_notcompiled_cv_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_ctpp2_set_template(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *ngx_http_ctpp2_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ctpp2_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_ctpp2_load_tmpl(ngx_conf_t *cf, u_char *path, ngx_buf_t *buffer);
static ngx_int_t ngx_http_ctpp2_filter_init(ngx_conf_t *cf);

static ngx_int_t ngx_strprepend_nulled(ngx_str_t *what, ngx_str_t *to, ngx_pool_t *pool);
static ngx_int_t ngx_strterminate(ngx_str_t *str, ngx_pool_t *pool);

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static ngx_command_t  ngx_http_ctpp2_filter_commands[] = {
	{
		ngx_string("ctpp2"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
		                  |NGX_CONF_FLAG,
		ngx_conf_set_flag_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_loc_conf_t, enable),
		NULL
	},
	{
		ngx_string("ctpp2_data_buffer"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
		                  |NGX_CONF_TAKE1,
		ngx_conf_set_size_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_loc_conf_t, buffer_size),
		NULL
	},
	{
		ngx_string("ctpp2_args_stack"),
		NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_MAIN_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_main_conf_t, args),
		NULL
	},
	{
		ngx_string("ctpp2_code_stack"),
		NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_MAIN_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_main_conf_t, code),
		NULL
	},
	{
		ngx_string("ctpp2_max_functions"),
		NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_MAIN_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_main_conf_t, funcs),
		NULL
	},
	{
		ngx_string("ctpp2_steps_limit"),
		NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_MAIN_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_main_conf_t, steps),
		NULL
	},
	{
		ngx_string("templates_check"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
		                  |NGX_CONF_TAKE1,
		ngx_conf_set_flag_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_loc_conf_t, tmpls_check),
		NULL
	},
	{
		ngx_string("templates_root"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
		                  |NGX_CONF_TAKE1,
		ngx_http_set_notcompiled_cv_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_loc_conf_t, tmpls_root),
		NULL
	},
	{
		ngx_string("templates_header"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
		                  |NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_ctpp2_loc_conf_t, tmpls_header),
		NULL
	},
	{
		ngx_string("template"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
		                  |NGX_CONF_TAKE12,
		ngx_http_ctpp2_set_template,
		NGX_HTTP_LOC_CONF_OFFSET,
		0,
		NULL
	},
	ngx_null_command
};


static ngx_http_module_t  ngx_http_ctpp2_filter_module_ctx = {
	NULL,                                  /* preconfiguration */
	ngx_http_ctpp2_filter_init,            /* postconfiguration */

	ngx_http_ctpp2_create_main_conf,       /* create main configuration */
	ngx_http_ctpp2_init_main_conf,         /* init main configuration */

	NULL,                                  /* create server configuration */
	NULL,                                  /* merge server configuration */

	ngx_http_ctpp2_create_loc_conf,        /* create location configuration */
	ngx_http_ctpp2_merge_loc_conf          /* merge location configuration */
};


ngx_module_t  ngx_http_ctpp2_filter_module = {
	NGX_MODULE_V1,
	&ngx_http_ctpp2_filter_module_ctx,     /* module context */
	ngx_http_ctpp2_filter_commands,        /* module directives */
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
ngx_http_ctpp2_header_filter(ngx_http_request_t *r)
{
	ngx_http_ctpp2_loc_conf_t  *conf;
	ngx_http_ctpp2_ctx_t       *ctx;
	ngx_str_t                   root, *tmpl;
	off_t                       len;

	if (r->headers_out.status == NGX_HTTP_NOT_MODIFIED) {
		return ngx_http_next_header_filter(r);
	}
	
	if (ngx_http_get_module_ctx(r, ngx_http_ctpp2_filter_module)) {
		ngx_http_set_ctx(r, NULL, ngx_http_ctpp2_filter_module);
		return ngx_http_next_header_filter(r);
	}
	
	conf = ngx_http_get_module_loc_conf(r, ngx_http_ctpp2_filter_module);
	if (!conf->enable) return ngx_http_next_header_filter(r);
	
	tmpl = ngx_http_ctpp2_get_tmpl_header(r, &conf->tmpls_header);
	if (tmpl == NULL) {
		if (conf->tmpl == NULL) return ngx_http_next_header_filter(r);
		
		ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_ctpp2_ctx_t));
		if (ctx == NULL) return NGX_ERROR;
		if (conf->tmpl_cache == NULL) {
			tmpl = &ctx->tmpl_path;
			if (ngx_http_complex_value(r, conf->tmpl, tmpl) != NGX_OK) {
				return NGX_ERROR;
			}
		} else {
			tmpl = &conf->tmpl->value;
			ctx->tmpl = conf->tmpl_cache;
			ctx->template_ready = 1;
		}
	} else {
		if (!ngx_path_separator(tmpl->data[0])) {
			if (ngx_http_complex_value(r, conf->tmpls_root, &root) != NGX_OK) {
				return NGX_ERROR;
			}
			if (ngx_strprepend_nulled(&root, tmpl, r->pool) != NGX_OK) {
				return NGX_ERROR;
			}
		}
		ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_ctpp2_ctx_t));
		if (ctx == NULL) return NGX_ERROR;
		ctx->tmpl_path = *tmpl;
	}
	ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
		"http ctpp2: Template \"%s\" will be processed", tmpl->data);
	
	len = r->headers_out.content_length_n;
	if (len == -1) {
		ngx_log_debug0(NGX_LOG_NOTICE, r->connection->log, 0,
			"Missing Content-Length header causes allocating default buffer size");
		len = conf->buffer_size;
	}
	
	ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
		"http ctpp2: Allocating %d bytes for data buffer", len);
	ctx->data = ngx_create_temp_buf(r->pool, len);
	if (ctx->data == NULL) return NGX_ERROR;
	
	r->main_filter_need_in_memory = 1;
	ngx_http_set_ctx(r, ctx, ngx_http_ctpp2_filter_module);
	
	return NGX_OK;
}


static ngx_str_t *
ngx_http_ctpp2_get_tmpl_header(ngx_http_request_t *r, ngx_str_t *name)
{
	ngx_list_part_t  *part;
	ngx_table_elt_t  *h;
	ngx_uint_t        i;

	part = &r->headers_out.headers.part;
	h = part->elts;

	for (i = 0; /* void */ ; i++) {
		if (i >= part->nelts) {
			if (part->next == NULL) break;
			
			part = part->next;
			h = part->elts;
			i = 0;
		}
		
		if (name->len != h[i].key.len || ngx_strcasecmp(name->data, h[i].key.data) != 0) {
			continue;
		}
		h[i].hash = 0;
		return &h[i].value;
	}

	return NULL;
}


static ngx_int_t
ngx_http_ctpp2_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
	ngx_http_ctpp2_ctx_t       *ctx;
	ngx_int_t                   rc;
	ngx_log_t                  *log;
	ngx_http_ctpp2_loc_conf_t  *conf;
	ngx_buf_t                  *b;
	ngx_chain_t                *out;
	size_t                      out_size;
	
	if (in == NULL) {
		return ngx_http_next_body_filter(r, in);
	}
	
	ctx = ngx_http_get_module_ctx(r, ngx_http_ctpp2_filter_module);
	if (ctx == NULL) {
		return ngx_http_next_body_filter(r, in);
	}
	
	log = r->connection->log;
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "http ctpp2 filter");


	if (!ctx->template_ready) {
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0,
			"http ctpp2: Filling template buffer");
		switch (ngx_http_ctpp2_fillbuffer(ctx->tmpl, &in)) {
			case NGX_AGAIN: return NGX_OK;
			case NGX_OK: break;
			default:
				ngx_log_error(NGX_LOG_ERR, log, 0,
					"Filling template buffer failed");
				return ngx_http_filter_finalize_request(r, &ngx_http_ctpp2_filter_module,
					NGX_HTTP_INTERNAL_SERVER_ERROR);
		}
		ctx->template_ready = 1;
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0,
			"http ctpp2: Template buffer filled");
		
		conf = ngx_http_get_module_loc_conf(r, ngx_http_ctpp2_filter_module);
		if (ctpp2_tmpltest(ctx->tmpl, conf->tmpls_check, log) != NGX_OK) {
			return ngx_http_filter_finalize_request(r, &ngx_http_ctpp2_filter_module,
				NGX_HTTP_INTERNAL_SERVER_ERROR);
		}
		
		if (in == NULL) return NGX_OK;
	}


	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0,
		"http ctpp2: Filling data buffer");
	switch (ngx_http_ctpp2_fillbuffer(ctx->data, &in)) {
		case NGX_AGAIN: return NGX_OK;
		case NGX_DONE: break;
		default:
			if (in == NULL) return NGX_OK;
			b = in->buf;
			if ((b->last_buf || b->last_in_chain) && b->pos == b->last) {
				break;
			}
			ngx_log_error(NGX_LOG_ERR, log, 0,
				"Data buffer overflow. You must set proper Content-Length header or, \
increase size of \"ctpp2_data_buffer\" in nginx conf");
			return ngx_http_filter_finalize_request(r, &ngx_http_ctpp2_filter_module,
				NGX_HTTP_INTERNAL_SERVER_ERROR);
	}
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0,
		"http ctpp2: Data buffer filled");

	if (ctpp2_process(ctx->tmpl, ctx->data, r->pool, &out, &out_size, log) != NGX_DONE) {
		return ngx_http_filter_finalize_request(r, &ngx_http_ctpp2_filter_module,
			NGX_HTTP_INTERNAL_SERVER_ERROR);
	}
	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0,
		"http ctpp2: Templating done");

	if (ctx->tmpl->temporary) ngx_pfree(r->pool, ctx->tmpl->start);
	ngx_http_set_ctx(r, NULL, ngx_http_ctpp2_filter_module);


	if (r == r->main) {
		ngx_http_clear_accept_ranges(r);
		r->headers_out.content_length_n = out_size;
		if (r->headers_out.content_length) {
			r->headers_out.content_length->hash = 0;
			r->headers_out.content_length = NULL;
		}
	}

	rc = ngx_http_next_header_filter(r);
	if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) return NGX_ERROR;

	if (out) {
		rc = ngx_http_next_body_filter(r, out);
		if (rc == NGX_ERROR) return rc;
	}

	return ngx_http_send_special(r, NGX_HTTP_LAST);
}


static ngx_int_t
ngx_http_ctpp2_fillbuffer(ngx_buf_t *buf, ngx_chain_t **in)
{
	ngx_chain_t         *cl;
	ngx_buf_t           *b;
	u_char              *p;
	size_t               size, rest;
	
	p = buf->last;
	rest = buf->end - p;
	cl = *in;
	
	do {
		b = cl->buf;
		
		size = b->last - b->pos;
		size = (rest < size) ? rest : size;
		
		ngx_memcpy(p, b->pos, size);
		rest = rest - size;
		
		if (!rest) {
			buf->last = buf->end;
			
			b->pos = b->pos + size;
			if (b->last == b->pos) {
				if (b->last_buf || b->last_in_chain) return NGX_DONE;
				*in = cl->next;
			} else {
				*in = cl;
			}
			
			return NGX_OK;
		}
		b->pos = b->last;
		p = p + size;
		cl = cl->next;
	} while (cl);
		
	*in = NULL;
	buf->last = p;
	
	if (b->last_buf || b->last_in_chain) return NGX_DONE;
	
	return NGX_AGAIN;
}


static void *
ngx_http_ctpp2_create_main_conf(ngx_conf_t *cf)
{
	ngx_http_ctpp2_main_conf_t  *mcf;

	mcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ctpp2_main_conf_t));
	if (mcf == NULL) return NULL;
	
	mcf->args  = NGX_CONF_UNSET_UINT;
	mcf->code  = NGX_CONF_UNSET_UINT;
	mcf->funcs = NGX_CONF_UNSET_UINT;
	mcf->steps = NGX_CONF_UNSET_UINT;

	return mcf;
}


static char *
ngx_http_ctpp2_init_main_conf(ngx_conf_t *cf, void *conf)
{
	ngx_http_ctpp2_main_conf_t *mcf = conf;
	
	if (mcf->args == NGX_CONF_UNSET_UINT) {
		mcf->args = 8192;
	}
	
	if (mcf->code == NGX_CONF_UNSET_UINT) {
		mcf->code = 8192;
	}
	
	if (mcf->funcs == NGX_CONF_UNSET_UINT) {
		mcf->funcs = 100;
	}
	
	if (mcf->steps == NGX_CONF_UNSET_UINT) {
		mcf->steps = 10240;
	}
	
	return NGX_CONF_OK;
}


static void *
ngx_http_ctpp2_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_ctpp2_loc_conf_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ctpp2_loc_conf_t));
	if (conf == NULL) return NULL;
	
	conf->enable = NGX_CONF_UNSET;
	conf->buffer_size = NGX_CONF_UNSET_SIZE;
	conf->tmpls_check = NGX_CONF_UNSET;

	return conf;
}


char *
ngx_http_set_notcompiled_cv_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	char  *p = conf;
	
	ngx_http_complex_value_t  **cv;

	cv = (ngx_http_complex_value_t **) (p + cmd->offset);
	if (*cv != NULL) return "duplicate";
	
	*cv = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
	if (*cv == NULL) return NGX_CONF_ERROR;
	
	(*cv)->value = ((ngx_str_t*) cf->args->elts)[1];

	return NGX_CONF_OK;
}


static char *
ngx_http_ctpp2_set_template(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
	ngx_http_ctpp2_loc_conf_t *lcf = conf;
	cmd->offset = offsetof(ngx_http_ctpp2_loc_conf_t, tmpl);
	
	ngx_str_t  *value;
	char       *ret;
	ngx_buf_t  *buf;

	if (cf->args->nelts == 2) {
		return ngx_http_set_notcompiled_cv_slot(cf, cmd, conf);
	}
	
	value = cf->args->elts;
	if (ngx_strcmp(value[1].data, "cached")) {
		return "invalid value";
	}
	if (ngx_http_script_variables_count(&value[2]) > 0) {
		return "variables in cached template path";
	}
	cf->args->elts = value + 1;
	ret = ngx_http_set_notcompiled_cv_slot(cf, cmd, conf);
	cf->args->elts = value; /* recover initial pointer */
	
	buf = ngx_calloc_buf(cf->pool);
	if (buf == NULL) return NGX_CONF_ERROR;
	lcf->tmpl_cache = buf;

	return ret;
}


static char *
ngx_http_ctpp2_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
	ngx_http_ctpp2_loc_conf_t *prev = parent;
	ngx_http_ctpp2_loc_conf_t *conf = child;
	
	ngx_http_ctpp2_main_conf_t  *mcf;
	ngx_str_t  *p_str, *c_str;
	ngx_http_compile_complex_value_t  ccv;

	ngx_conf_merge_value(conf->enable, prev->enable, 0);
	if (conf->enable) {
		mcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_ctpp2_filter_module);
		if (ctpp2_init(mcf->args, mcf->code, mcf->funcs, mcf->steps) != NGX_OK) {
			return NGX_CONF_ERROR;
		}
	}
	
	ngx_conf_merge_size_value(conf->buffer_size, prev->buffer_size, 16 * 1024);
	ngx_conf_merge_value(conf->tmpls_check, prev->tmpls_check, 0);
	ngx_conf_merge_str_value(conf->tmpls_header, prev->tmpls_header, NGX_HTTP_CTPP2_TMPLS_HEADER);
	
	if (conf->tmpls_root == NULL) {
		if (prev->tmpls_root == NULL) {
			conf->tmpls_root = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
			if (conf->tmpls_root == NULL) return NGX_CONF_ERROR;
			ngx_str_set(&conf->tmpls_root->value, NGX_CTPP2_TMPLS_ROOT_PATH "/");
		} else {
			conf->tmpls_root = prev->tmpls_root;
		}
	} else {
		c_str = &conf->tmpls_root->value;
		p_str = &prev->tmpls_root->value;
		if (!ngx_path_separator(c_str->data[0]) && prev->tmpls_root != NULL && p_str->data != NULL) {
			if (ngx_strprepend_nulled(p_str, c_str, cf->pool) != NGX_OK) {
				return NGX_CONF_ERROR;
			}	
		} else {
			if (!ngx_path_separator(c_str->data[c_str->len - 1])) {
				if (ngx_strterminate(c_str, cf->pool) != NGX_OK) {
					return NGX_CONF_ERROR;
				}
			}
		}
		if (!ngx_path_separator(c_str->data[c_str->len - 1])) {
			c_str->data[c_str->len] = '/';
			c_str->len++;
		}
		
		ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
		
		ccv.cf = cf;
		ccv.value = c_str;
		ccv.complex_value = conf->tmpls_root;
		
		if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
			return NGX_CONF_ERROR;
		}
	}
	
	if (conf->tmpl == NULL) {
		conf->tmpl = prev->tmpl;
		conf->tmpl_cache = prev->tmpl_cache;
	} else {
		c_str = &conf->tmpl->value;
		if (!ngx_path_separator(c_str->data[0])) {
			p_str = &conf->tmpls_root->value;
			if (ngx_strprepend_nulled(p_str, c_str, cf->pool) != NGX_OK) {
				return NGX_CONF_ERROR;
			}
			if (!ngx_path_separator(c_str->data[0]) && ngx_conf_full_name(cf->cycle, c_str, 0) != NGX_OK) {
				return NGX_CONF_ERROR;
			}
		} else {
			if (ngx_strterminate(c_str, cf->pool) != NGX_OK) {
				return NGX_CONF_ERROR;
			}
		}
		
		if (conf->tmpl_cache == NULL) {
			ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
			
			ccv.cf = cf;
			ccv.value = c_str;
			ccv.zero = 1;
			ccv.complex_value = conf->tmpl;
			
			if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
				return NGX_CONF_ERROR;
			}
		} else {
			if (ngx_http_script_variables_count(c_str) > 0) {
				ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
					"can't cache template with relative path and variable root: \"%s\"", c_str->data);
				return NGX_CONF_ERROR;
			}
			if (ngx_http_ctpp2_load_tmpl(cf, c_str->data, conf->tmpl_cache) != NGX_OK) {
				ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
					"load template \"%s\" to cache failed", c_str->data);
				return NGX_CONF_ERROR;
			}
		}
	}

	return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_ctpp2_load_tmpl(ngx_conf_t *cf, u_char *path, ngx_buf_t *buffer)
{
	ngx_fd_t         fd;
	ngx_file_info_t  fi;
	off_t     size;
	u_char   *b;
	ssize_t   n;

	fd = ngx_open_file(path, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
	if (fd == NGX_INVALID_FILE) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno, ngx_open_file_n " \"%s\" failed", path);
		return NGX_ERROR;
	}
	if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno, ngx_fd_info_n " \"%s\" failed", path);
		if (ngx_close_file(fd) == NGX_FILE_ERROR) {
			ngx_conf_log_error(NGX_LOG_ALERT, cf, ngx_errno, ngx_close_file_n " \"%s\" failed", path);
		}
		return NGX_ERROR;
	}
	size = ngx_file_size(&fi);
	b = ngx_palloc(cf->pool, size);
	if (b == NULL) return NGX_ERROR;
	
	n = ngx_read_fd(fd, b, (size_t) size);
	if (n == NGX_FILE_ERROR) {
		ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno, ngx_read_fd_n " \"%s\" failed", path);
		return NGX_ERROR;
	}
	if (ngx_close_file(fd) == NGX_FILE_ERROR) {
		ngx_conf_log_error(NGX_LOG_ALERT, cf, ngx_errno, ngx_close_file_n " \"%s\" failed", path);
	}
	
	buffer->start = b;
	buffer->end   = b + size;
	buffer->pos   = buffer->start;
	buffer->last  = buffer->end;
	if (ctpp2_tmpltest(buffer, 1, cf->log) != NGX_OK) {
		return NGX_ERROR;
	}

	return NGX_OK;
}


static ngx_int_t
ngx_http_ctpp2_filter_init(ngx_conf_t *cf)
{
	ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_http_ctpp2_header_filter;

	ngx_http_next_body_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_http_ctpp2_body_filter;

	return NGX_OK;
}


static ngx_int_t
ngx_strprepend_nulled(ngx_str_t *what, ngx_str_t *to, ngx_pool_t *pool)
{
	size_t   s;
	u_char  *b;

	s = what->len + to->len;
	b = (u_char *) ngx_pnalloc(pool, s+1);
	if (b == NULL) return NGX_ERROR;
	
	ngx_memcpy(b, what->data, what->len);
	ngx_memcpy(b + what->len, to->data, to->len);
	b[s] = '\0';
	to->data = b;
	to->len = s;

	return NGX_OK;
}

static ngx_int_t
ngx_strterminate(ngx_str_t *str, ngx_pool_t *pool)
{
	u_char  *b;

	b = (u_char *) ngx_pnalloc(pool, str->len + 1);
	if (b == NULL) return NGX_ERROR;
	
	ngx_memcpy(b, str->data, str->len);
	b[str->len] = '\0';
	str->data = b;

	return NGX_OK;
}

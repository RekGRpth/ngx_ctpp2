
/*
 * Copyright (C) Valentin V. Bartenev
 */


#ifdef __cplusplus
extern "C" {
#endif

#include <ngx_config.h>
#include <ngx_core.h>

ngx_int_t ctpp2_init(
	ngx_uint_t  args,
	ngx_uint_t  code,
	ngx_uint_t  funcs,
	ngx_uint_t  steps
);

ngx_int_t ctpp2_tmpltest(ngx_buf_t *tmpl, ngx_flag_t check, ngx_log_t *log);

ngx_int_t ctpp2_process(
	ngx_buf_t    *tmpl,
	ngx_buf_t    *data,
	ngx_pool_t   *pool,
	ngx_chain_t **out,
	size_t       *out_size,
	ngx_log_t    *log
);

#ifdef __cplusplus
}
#endif

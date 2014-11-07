/**
 * snowflake nginx module
 * 2014-11-02
 **/

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    ngx_msec_t epoch; 
    ngx_msec_t last_timestamp;
    ngx_uint_t server_id;
    ngx_uint_t server_id_mask;
    ngx_uint_t worker_id;
    ngx_uint_t worker_id_mask;
    ngx_uint_t sequence;
    ngx_uint_t sequence_mask;
    /* default 12 5 5 */
    ngx_uint_t sequence_bits;
    ngx_uint_t server_id_bits;
    ngx_uint_t worker_id_bits;
} ngx_http_snowflake_loc_conf_t;

static char * ngx_http_snowflake_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void * ngx_http_snowflake_create_loc_conf(ngx_conf_t *cf);
static ngx_msec_t sf_timestamp();
static ngx_uint_t sf_id(ngx_http_snowflake_loc_conf_t *conf);

static ngx_command_t  ngx_http_snowflake_commands[] = {
    { ngx_string("snowflake"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_snowflake_conf,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("sf_epoch"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_snowflake_loc_conf_t, epoch),
      NULL },

    { ngx_string("sf_server_id"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_snowflake_loc_conf_t, server_id),
      NULL },

    { ngx_string("sf_sequence_bits"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_snowflake_loc_conf_t, sequence_bits),
      NULL },

    { ngx_string("sf_server_id_bits"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_snowflake_loc_conf_t, server_id_bits),
      NULL },

    { ngx_string("sf_worker_id_bits"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_snowflake_loc_conf_t, worker_id_bits),
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_snowflake_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    ngx_http_snowflake_create_loc_conf,  /* create location configuration */
    NULL                                 /* merge location configuration */
};

ngx_module_t  ngx_http_snowflake_module = {
    NGX_MODULE_V1,
    &ngx_http_snowflake_module_ctx, /* module context */
    ngx_http_snowflake_commands,   /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};

static void *
ngx_http_snowflake_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_snowflake_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_snowflake_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->epoch = NGX_CONF_UNSET_MSEC;
    conf->server_id = NGX_CONF_UNSET_UINT;
    conf->server_id_bits = NGX_CONF_UNSET_UINT;
    conf->worker_id = NGX_CONF_UNSET_UINT;
    conf->worker_id_bits = NGX_CONF_UNSET_UINT;
    conf->sequence_bits = NGX_CONF_UNSET_UINT;

    return conf;
}

static ngx_int_t
ngx_http_snowflake_handler(ngx_http_request_t *r)
{
    ngx_buf_t    *b;
    ngx_chain_t   out;
    char *body;
    off_t body_len = 0;
    ngx_uint_t id = 0;

    ngx_http_snowflake_loc_conf_t  *conf;
    conf = ngx_http_get_module_loc_conf(r, ngx_http_snowflake_module);

    if(conf->worker_id == NGX_CONF_UNSET_UINT)
        conf->worker_id = getpid();

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
            "Failed to allocate response buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    body_len = sizeof("{\"id\":\"\",")
        + sizeof("\"server_id\":\"\",")
	+ sizeof("\"worker_id\":\"\",")
	+ sizeof("\"timestamp\":\"\"}");
    body = ngx_pcalloc(r->pool, body_len + 128);
    if (body == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
            "Failed to allocate json buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    id = sf_id(conf);

    body_len += snprintf(body, 128,
        "{\"id\":\"%lX\",\"server_id\":\"%lu\",\"worker_id\":\"%lu\",\"timestamp\":\"%lu\"}",
	id,
        conf->server_id,
        conf->worker_id,
	conf->last_timestamp);

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = body_len;
    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";
    ngx_http_send_header(r);

    b->pos = (u_char *)body;
    b->last = (u_char *)(body + body_len);
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}

static char *
ngx_http_snowflake_conf(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_snowflake_loc_conf_t *sf_conf = conf;

    if(sf_conf->server_id == NGX_CONF_UNSET_UINT) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "sf_server_id must be set, when using snowflake"); 
        return NGX_CONF_ERROR;
    }

    if(sf_conf->epoch == NGX_CONF_UNSET_MSEC)
        sf_conf->epoch = 1288834974657L;
    if(sf_conf->server_id_bits == NGX_CONF_UNSET_UINT)
        sf_conf->server_id_bits = 5;
    if(sf_conf->worker_id_bits == NGX_CONF_UNSET_UINT)
        sf_conf->worker_id_bits = 5;
    if(sf_conf->sequence_bits == NGX_CONF_UNSET_UINT)
        sf_conf->sequence_bits = 12;

    sf_conf->last_timestamp = 0;
    sf_conf->sequence = 0;

    sf_conf->server_id_mask = -1L ^ (-1L << sf_conf->server_id_bits);
    sf_conf->worker_id_mask = -1L ^ (-1L << sf_conf->worker_id_bits);
    sf_conf->sequence_mask  = -1L ^ (-1L << sf_conf->sequence_bits);

    if(sf_conf->server_id > sf_conf->server_id_mask) {
         ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
            "sf_server_id is too long, should be cut off"); 
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_snowflake_handler;

    return NGX_CONF_OK;
}

static ngx_msec_t
sf_timestamp()
{    
    struct timeval tv;    
    gettimeofday(&tv, NULL);    
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;    
}    

static ngx_uint_t
sf_id(ngx_http_snowflake_loc_conf_t *conf)
{
    ngx_msec_t timestamp = sf_timestamp();

    if(timestamp == conf->last_timestamp) {
        conf->sequence = (conf->sequence + 1) & conf->sequence_mask;
	if(conf->sequence == 0) {
	    timestamp = sf_timestamp();
        }
    }
    else {
        conf->sequence = 0;
    }

    conf->last_timestamp = timestamp;

    timestamp = (timestamp - conf->epoch)
        << (conf->sequence_bits + conf->server_id_bits + conf->worker_id_bits);

    return timestamp
       | (conf->sequence << (conf->server_id_bits + conf->worker_id_bits))
       | ((conf->server_id & conf->server_id_mask) << conf->worker_id_bits)
       | (conf->worker_id & conf->worker_id_mask);
}

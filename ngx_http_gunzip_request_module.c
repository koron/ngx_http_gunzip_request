#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <zlib.h>

typedef struct {
    ngx_flag_t           enable;
    ngx_bufs_t           bufs;
    size_t               max_inflate_size;
} ngx_http_gunzip_request_conf_t;


typedef struct {
    ngx_chain_t         *in;
    ngx_chain_t         *free;
    ngx_chain_t         *busy;
    ngx_chain_t         *out;
    ngx_chain_t        **last_out;

    ngx_buf_t           *in_buf;
    ngx_buf_t           *out_buf;
    ngx_int_t            bufs;

    unsigned             started:1;
    unsigned             flush:4;
    unsigned             redo:1;
    unsigned             done:1;
    unsigned             nomem:1;

    unsigned             skip:1;
    unsigned             checked:1;

    size_t               sum;

    z_stream             zstream;
    ngx_http_request_t  *request;
} ngx_http_gunzip_request_ctx_t;


static ngx_int_t ngx_http_gunzip_request_init(ngx_conf_t *cf);
static void *ngx_http_gunzip_request_create_conf(ngx_conf_t *cf);
static char *ngx_http_gunzip_request_merge_conf(ngx_conf_t *cf,
    void *parent, void *child);


static ngx_command_t  ngx_http_gunzip_request_commands[] = {

    { ngx_string("gunzip_request"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_gunzip_request_conf_t, enable),
      NULL },

    { ngx_string("gunzip_request_buffers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_bufs_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_gunzip_request_conf_t, bufs),
      NULL },

    { ngx_string("gunzip_request_max_inflate_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_gunzip_request_conf_t, max_inflate_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_gunzip_request_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_gunzip_request_init,          /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_gunzip_request_create_conf,           /* create location configuration */
    ngx_http_gunzip_request_merge_conf             /* merge location configuration */
};


ngx_module_t  ngx_http_gunzip_request_module = {
    NGX_MODULE_V1,
    &ngx_http_gunzip_request_module_ctx,   /* module context */
    ngx_http_gunzip_request_commands,      /* module directives */
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


static ngx_http_request_body_filter_pt   ngx_http_next_request_body_filter;


static void *
ngx_http_gunzip_request_create_conf(ngx_conf_t *cf)
{
    ngx_http_gunzip_request_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_gunzip_request_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;

    conf->max_inflate_size = NGX_CONF_UNSET_SIZE;

    return conf;
}


static char *
ngx_http_gunzip_request_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_gunzip_request_conf_t *prev = parent;
    ngx_http_gunzip_request_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);

    ngx_conf_merge_bufs_value(conf->bufs, prev->bufs,
                              (128 * 1024) / ngx_pagesize, ngx_pagesize);

    ngx_conf_merge_size_value(conf->max_inflate_size, prev->max_inflate_size, 0);

    return NGX_CONF_OK;
}

static void *
ngx_http_gunzip_request_alloc(void *opaque, u_int items, u_int size)
{
    ngx_http_gunzip_request_ctx_t *ctx = opaque;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ctx->request->connection->log, 0,
                   "[gunzreq] gunzip alloc: n:%ud s:%ud",
                   items, size);

    return ngx_palloc(ctx->request->pool, items * size);
}


static void
ngx_http_gunzip_request_free(void *opaque, void *address)
{
#if 0
    ngx_http_gunzip_request_ctx_t *ctx = opaque;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ctx->request->connection->log, 0,
                   "[gunzreq] gunzip free: %p", address);
#endif
}

static ngx_int_t
ngx_http_gunzip_request_inflate_start(ngx_http_request_t *r,
    ngx_http_gunzip_request_ctx_t *ctx)
{
    int  rc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] inflate start");

    ctx->request = r;

    ctx->zstream.next_in = Z_NULL;
    ctx->zstream.avail_in = 0;

    ctx->zstream.zalloc = ngx_http_gunzip_request_alloc;
    ctx->zstream.zfree = ngx_http_gunzip_request_free;
    ctx->zstream.opaque = ctx;

    /* windowBits +16 to decode gzip, zlib 1.2.0.4+ */
    rc = inflateInit2(&ctx->zstream, MAX_WBITS + 16);

    if (rc != Z_OK) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "[gunzreq] inflateInit2() failed: %d", rc);
        return NGX_ERROR;
    }

    ctx->started = 1;

    ctx->last_out = &ctx->out;
    ctx->flush = Z_NO_FLUSH;

    return NGX_OK;
}

static ngx_int_t
ngx_http_gunzip_request_inflate_end(ngx_http_request_t *r,
    ngx_http_gunzip_request_ctx_t *ctx)
{
    int           rc;
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] gunzip inflate end");

    rc = inflateEnd(&ctx->zstream);

    if (rc != Z_OK) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "[gunzreq] inflateEnd() failed: %d", rc);
        return NGX_ERROR;
    }

    b = ctx->out_buf;

    // update content_length_n
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] recv_sum=%d", ctx->sum);
    r->headers_in.content_length_n = ctx->sum;

    if (ngx_buf_size(b) == 0) {

        b = ngx_calloc_buf(ctx->request->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }
    }

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    cl->next = NULL;
    *ctx->last_out = cl;
    ctx->last_out = &cl->next;

    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;
    b->sync = 1;

    ctx->done = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_http_gunzip_request_add_data(ngx_http_request_t *r,
    ngx_http_gunzip_request_ctx_t *ctx)
{
    if (ctx->zstream.avail_in || ctx->flush != Z_NO_FLUSH || ctx->redo) {
        return NGX_OK;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] in: %p (%d)", ctx->in, ctx->in != NULL ? ngx_buf_size(ctx->in->buf): -1);

    if (ctx->in == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] add_data case#5");
        return NGX_DECLINED;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] in#2: size=%d next=%p", ngx_buf_size(ctx->in->buf), ctx->in->next);

    ctx->in_buf = ctx->in->buf;
    ctx->in = ctx->in->next;

    ctx->zstream.next_in = ctx->in_buf->pos;
    ctx->zstream.avail_in = ctx->in_buf->last - ctx->in_buf->pos;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] in_buf:%p ni:%p ai:%ud",
                   ctx->in_buf,
                   ctx->zstream.next_in, ctx->zstream.avail_in);

    if (ctx->in_buf->last_buf || ctx->in_buf->last_in_chain) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] add_data case#1");
        ctx->flush = Z_FINISH;

    } else if (ctx->in_buf->flush) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] add_data case#2");
        ctx->flush = Z_SYNC_FLUSH;

    } else if (ctx->zstream.avail_in == 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] add_data case#3");
        return NGX_AGAIN;
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] add_data case#0");
    }

    return NGX_OK;
}

static ngx_int_t
ngx_http_gunzip_request_get_buf(ngx_http_request_t *r,
    ngx_http_gunzip_request_ctx_t *ctx)
{
    ngx_http_gunzip_request_conf_t  *conf;

    if (ctx->zstream.avail_out) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] get_buf: case#0");
        return NGX_OK;
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_gunzip_request_module);

    if (ctx->free) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] get_buf: case#1");
        ctx->out_buf = ctx->free->buf;
        ctx->free = ctx->free->next;

        ctx->out_buf->flush = 0;

    } else if (ctx->bufs < conf->bufs.num) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] get_buf: case#2");

        ctx->out_buf = ngx_create_temp_buf(r->pool, conf->bufs.size);
        if (ctx->out_buf == NULL) {
            return NGX_ERROR;
        }

        ctx->out_buf->tag = (ngx_buf_tag_t) &ngx_http_gunzip_request_module;
        ctx->out_buf->recycled = 1;
        ctx->bufs++;

    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] get_buf: case#3");
        ctx->nomem = 1;
        return NGX_DECLINED;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] get_buf: case#4");
    ctx->zstream.next_out = ctx->out_buf->pos;
    ctx->zstream.avail_out = conf->bufs.size;

    return NGX_OK;
}

static ngx_int_t
ngx_http_gunzip_request_inflate(ngx_http_request_t *r,
    ngx_http_gunzip_request_ctx_t *ctx)
{
    int           rc;
    ngx_buf_t    *b;
    ngx_chain_t  *cl;
    size_t        curr;
    ngx_http_gunzip_request_conf_t *conf;

    curr = ctx->zstream.avail_out;
    ngx_log_debug6(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] inflate in: ni:%p no:%p ai:%ud ao:%ud fl:%d redo:%d",
                   ctx->zstream.next_in, ctx->zstream.next_out,
                   ctx->zstream.avail_in, ctx->zstream.avail_out,
                   ctx->flush, ctx->redo);

    rc = inflate(&ctx->zstream, ctx->flush);

    if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "[gunzreq] inflate() failed: %d, %d", ctx->flush, rc);
        return NGX_ERROR;
    }

    if (curr > ctx->zstream.avail_out) {
        ctx->sum += curr - ctx->zstream.avail_out;
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "[gunzreq] inflate and update sum: %d", ctx->sum);

        // check overflow of inflate size against zip bomb
        conf = ngx_http_get_module_loc_conf(r, ngx_http_gunzip_request_module);
        if (conf->max_inflate_size > 0 && ctx->sum > conf->max_inflate_size) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                    "[gunzreq] overflow max inflate size: %d > %d", ctx->sum, conf->max_inflate_size);
            return NGX_DECLINED;
        }
    }
    ngx_log_debug5(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] inflate out: ni:%p no:%p ai:%ud ao:%ud rc:%d",
                   ctx->zstream.next_in, ctx->zstream.next_out,
                   ctx->zstream.avail_in, ctx->zstream.avail_out,
                   rc);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "[gunzreq] gunzip in_buf:%p pos:%p",
                   ctx->in_buf, ctx->in_buf->pos);

    if (ctx->zstream.next_in) {
        ctx->in_buf->pos = ctx->zstream.next_in;

        if (ctx->zstream.avail_in == 0) {
            ctx->zstream.next_in = NULL;
        }
    }

    ctx->out_buf->last = ctx->zstream.next_out;

    if (ctx->zstream.avail_out == 0) {

        /* zlib wants to output some more data */

        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        cl->buf = ctx->out_buf;
        cl->next = NULL;
        *ctx->last_out = cl;
        ctx->last_out = &cl->next;

        ctx->redo = 1;

        return NGX_AGAIN;
    }

    ctx->redo = 0;

    if (ctx->flush == Z_SYNC_FLUSH) {

        ctx->flush = Z_NO_FLUSH;

        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        b = ctx->out_buf;

        if (ngx_buf_size(b) == 0) {

            b = ngx_calloc_buf(ctx->request->pool);
            if (b == NULL) {
                return NGX_ERROR;
            }

        } else {
            ctx->zstream.avail_out = 0;
        }

        b->flush = 1;

        cl->buf = b;
        cl->next = NULL;
        *ctx->last_out = cl;
        ctx->last_out = &cl->next;

        return NGX_OK;
    }

    if (ctx->flush == Z_FINISH && ctx->zstream.avail_in == 0) {

        if (rc != Z_STREAM_END) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "[gunzreq] inflate() returned %d on response end", rc);
            return NGX_ERROR;
        }

        if (ngx_http_gunzip_request_inflate_end(r, ctx) != NGX_OK) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    if (rc == Z_STREAM_END && ctx->zstream.avail_in > 0) {

        rc = inflateReset(&ctx->zstream);

        if (rc != Z_OK) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                          "[gunzreq] inflateReset() failed: %d", rc);
            return NGX_ERROR;
        }

        ctx->redo = 1;

        return NGX_AGAIN;
    }

    if (ctx->in == NULL) {

        b = ctx->out_buf;

        if (ngx_buf_size(b) == 0) {
            return NGX_OK;
        }

        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        ctx->zstream.avail_out = 0;

        cl->buf = b;
        cl->next = NULL;
        *ctx->last_out = cl;
        ctx->last_out = &cl->next;

        return NGX_OK;
    }

    return NGX_AGAIN;
}

static ngx_int_t
ngx_http_gunzip_request_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_gunzip_request_conf_t *conf;
    ngx_uint_t              i;
    ngx_list_part_t        *part;
    ngx_table_elt_t        *header;
    ngx_int_t               decompress = 0;
    ngx_http_gunzip_request_ctx_t  *ctx;
    ngx_uint_t              flush;
    ngx_chain_t            *cl;
    int                     rc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] filter invoked");

    for (cl = in; cl; cl = cl->next) {
        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, r->connection->log, 0,
                       "[gunzreq] new buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_gunzip_request_module);
    if (!conf->enable) {
        return ngx_http_next_request_body_filter(r, in);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_gunzip_request_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_gunzip_request_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }
        ngx_http_set_ctx(r, ctx, ngx_http_gunzip_request_module);
    }

    if (ctx->done || ctx->skip) {
        return ngx_http_next_request_body_filter(r, in);
    }

    if (!ctx->checked) {
        part = &r->headers_in.headers.part;
        header = part->elts;
        for (i = 0; /* void */; i++) {
            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                header = part->elts;
                i = 0;
            }
            if (header[i].key.len == sizeof("Content-Encoding") - 1
                    && ngx_strncasecmp(header[i].key.data,
                        (u_char *) "Content-Encoding",
                        sizeof("Content-Encoding") - 1) == 0
                    && header[i].value.len == 4
                    && ngx_strncasecmp(header[i].value.data,
                        (u_char *) "gzip", 4) == 0)
            {
                ngx_str_set(&header[i].value, "identity");
                decompress = 1;
                break;
            }
        }

        ctx->checked = 1;
        if (!decompress) {
            ctx->skip = 1;
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] thru");
            rc = ngx_http_next_request_body_filter(r, in);
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] thru next post: rc=%d busy.size=%d", rc, (ctx->busy != NULL ? ngx_buf_size(ctx->busy->buf) : -1));
            return rc;
        }
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] decompress request body");

    ctx = ngx_http_get_module_ctx(r, ngx_http_gunzip_request_module);
    if (ctx != NULL && ctx->done) {
        return ngx_http_next_request_body_filter(r, in);
    }
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_gunzip_request_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }
        ngx_http_set_ctx(r, ctx, ngx_http_gunzip_request_module);
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "[gunzreq] available ctx: busy=%d", ctx->busy != 0);

    if (!ctx->started) {
        if (ngx_http_gunzip_request_inflate_start(r, ctx) != NGX_OK) {
            goto failed;
        }
    }

    if (in) {
        if (ngx_chain_add_copy(r->pool, &ctx->in, in) != NGX_OK) {
            goto failed;
        }
    }

    if (ctx->nomem) {
        /* flush busy buffers */
        if (ngx_http_next_request_body_filter(r, NULL) == NGX_ERROR) {
            goto failed;
        }
        cl = NULL;
        ngx_chain_update_chains(r->pool, &ctx->free, &ctx->busy, &cl,
                                (ngx_buf_tag_t) &ngx_http_gunzip_request_module);
        ctx->nomem = 0;
        flush = 0;
    } else {
        flush = ctx->busy ? 1 : 0;
    }

    for ( ;; ) {
        /* cycle while we can write to a client */
        for ( ;; ) {

            /* cycle while there is data to feed zlib and ... */
            rc = ngx_http_gunzip_request_add_data(r, ctx);
            if (rc == NGX_DECLINED) {
                break;
            }
            if (rc == NGX_AGAIN) {
                continue;
            }

            /* ... there are buffers to write zlib output */
            rc = ngx_http_gunzip_request_get_buf(r, ctx);
            if (rc == NGX_DECLINED) {
                goto entity_too_large;
            }
            if (rc == NGX_ERROR) {
                goto failed;
            }

            rc = ngx_http_gunzip_request_inflate(r, ctx);
            if (rc == NGX_OK) {
                break;
            }
            if (rc == NGX_ERROR) {
                goto failed;
            }
            if (rc == NGX_DECLINED) {
                goto entity_too_large;
            }
            /* rc == NGX_AGAIN */
        }

        if (ctx->out == NULL && !flush) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                    "[gunzreq] no out, no flush: busy=%d nomem=%d", ctx->busy != 0, ctx->nomem);
            return NGX_OK;
            return ctx->busy ? NGX_AGAIN : NGX_OK;
        }


        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] next pre: out.size=%d", (ctx->out != NULL ? ngx_buf_size(ctx->out->buf) : -1));
        rc = ngx_http_next_request_body_filter(r, ctx->out);
        if (rc == NGX_ERROR) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                    "[gunzreq] error");
            goto failed;
        }
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[gunzreq] next post: out.size=%d", (ctx->out != NULL ? ngx_buf_size(ctx->out->buf) : -1));

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "[gunzreq] update_chains#2 pre: busy=%d out.size=%d",
                       ctx->busy != 0,
                       (ctx->out != NULL ?
                       ngx_buf_size(ctx->out->buf) : -1));
        ngx_chain_update_chains(r->pool, &ctx->free, &ctx->busy, &ctx->out,
                                (ngx_buf_tag_t) &ngx_http_gunzip_request_module);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "[gunzreq] update_chains#2 post: busy=%d", ctx->busy != 0);
        ctx->last_out = &ctx->out;

        ctx->nomem = 0;
        flush = 0;

        if (ctx->done) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                    "[gunzreq] done: rc=%d", rc);
            return rc;
        }
    }

    /* unreachable */

entity_too_large:
    ctx->done = 1;
    (void) ngx_http_discard_request_body(r);
    ngx_http_finalize_request(r, NGX_HTTP_REQUEST_ENTITY_TOO_LARGE);
    return NGX_OK;

failed:
    ctx->done = 1;
    return NGX_ERROR;
}

static ngx_int_t
ngx_http_gunzip_request_init(ngx_conf_t *cf)
{
    ngx_http_next_request_body_filter = ngx_http_top_request_body_filter;
    ngx_http_top_request_body_filter = ngx_http_gunzip_request_body_filter;

    return NGX_OK;
}

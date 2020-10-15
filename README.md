# `ngx_http_gunzip_request`

This is nginx module that inflating gzipped requests.

## Build

To configure nginx to use `ngx_http_gunzip_request` module,
put `--add-module=/path/to/ngx_http_gunzip_request` for nginx's
`auto/configure`script.
Then build and install nginx as usual.

For example:

```console
$ ./auto/configure --prefix=/opt/nginx \
    --add-module=/path/to/ngx_http_gunzip_request
$ make
$ sudo make install
```

## Configuration

*   `gunzip_request` - boolean.
    enable this module for location.

*   `gunzip_request_buffers` - integer, optional.
    number of buffer pages for inflation.

These two configurations can be put into root level, `server` block, and
`location` block.

Example of partial nginx.conf:

```nginx
location /gunzip_request/ {
    gunzip_request on;

    # no need this anymore
    ## remove "Content-Encoding: gzip" header from request.
    #proxy_set_header content-encoding '';

    # general configurations for reverse proxy
    proxy_pass http://192.168.0.123:8000/;
    proxy_http_version 1.1;
    proxy_set_header connection '';

    # unlimit POST body size for tests
    #client_max_body_size 0;
}
```

## Dynamic module

To build `ngx_http_gunzip_request` as dynamic module, at first you should
configure with `--add-dynamic-module` option instead of `--add-module` option.

For example:

```console
$ ./auto/configure --prefix=/opt/nginx \
    --add-dynamic-module=/path/to/ngx_http_gunzip_request
$ make
$ sudo make install
```

Next add `load_module` directive at prior of nginx.conf file.

`load_module` ディレクティブはなるべく nginx.conf の先頭のほうに書いたほうが良いです。

```nginx
load_module /opt/nginx/modules/ngx_http_gunzip_request.so;

# your other configurations...
```

# `ngx_http_gunzip_request`

This is nginx module that inflating gzipped requests.

It works on requests with `Content-Encoding: gzip` header.  Inflate requests
and rewrite `Content-Encoding: identity` header, then pass those to upstream.

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

*   `gunzip_request_buffers` - buffer size, optional.
    number of buffer pages for inflation.

    It accepts two args: number of buffers, and size of one buffer.
    Size of buffer should same with page size of system.
    (Page size is `4k` for x86/Linux).

    Default is 128KB (`32 4k` or `16 8k`).

    展開後のメッセージのおおよその最大サイズ制限としても機能する。
    もしバッファに収まりきらない場合は 413 で失敗する。

*   `gunzip_request_max_inflate_size` - integer, optional.
    Limitate size of inflated request. If it exceeded the limit, nginx will
    fail with `413 Request Entity Too Large`.

    `gunzip_request_buffers` と合わせて指定する必要がある。

These configurations can be put into root level, `server` block, and
`location` block.

Example of partial nginx.conf:

```nginx
location /gunzip_request/ {
    gunzip_request on;

    # no need this anymore, this module rewrites it to "identity"
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

### Limitation on size of inflated request

gunzip展開後のリクエストのサイズには制限があります。このサイズ制限を超えると
nginxは `413 Request Entity Too Large` エラーをクライアントへ返します。

デフォルトでは 128KB で制限されます。これは `gunzip_request_buffers` のデフォル
ト値によるものです。制限値を大きくしたい場合はまずこの値を大きくしてください。

1MBに変更する例:

```nginx
gunzip_request_buffers 256 4k;
```

`gunzip_request_buffers` による制限だけではリクエストの内容次第では、それよりも
若干小さいサイズでもエラーになる場合が考えられます。より正確に制限したい場合に
は `gunzip_request_max_inflate_size` を合わせて指定してください。その際
`gunzip_request_buffers` には若干大きなサイズ(1.1～2倍程度)を指定してください。

```nginx
gunzip_request_buffers 281 4k; // 1割増し
gunzip_request_max_inflate_size 1m;
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

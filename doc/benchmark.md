# Benchmark note

## How to benchmark

### `GET`

Simple GET request

```console
$ ab -c 5 -n 100000 -k http://${TARGET_HOST}/test1/
```

### `GZ1`

POST 10K gzipped JSON, send it with deflate to backend

```console
$ ab -c 5 -n 100000 -k -T 'application/json' -H 'Content-Encoding: gzip' -p testdata/40KB.json.gz http://${TARGET_HOST}/test1/
```

### `GZ0`

POST 10K gzipped JSON, send it as is to backend

```console
$ ab -c 5 -n 100000 -k -T 'application/json' -p testdata/40KB.json.gz http://${TARGET_HOST}/test1/
```

### `RAW0`

POST 40K raw JSON send it as is to backend (without gunzip requet feature)

```console
$ ab -c 5 -n 100000 -k -T 'application/json' -p testdata/40KB.json http://${TARGET_HOST}/test0/
```

### `RAW1`

POST 40K raw JSON send it as is to backend (with gunzip requet feature)

```console
$ ab -c 5 -n 100000 -k -T 'application/json' -p testdata/40KB.json http://${TARGET_HOST}/test1/
```

## Result

### 2020-09-30

ab + (nginx+gunzip request) + httpreqinfo

Case |N    |C  |R#/s |CPU Load
-----|----:|--:|----:|-------:
GET  |100K |5  |10K  |5±3%
GZ1  |100K |5  |2.2K |15±3%
GZ0  |100K |5  |5.2K |5±3%
RAW0 |100K |5  |1.8K |12±3%
RAW1 |100K |5  |1.8K |12±3%

* 特段の過負荷はなく、想定される理論値を裏切らない
* GZ0 が当初より早い

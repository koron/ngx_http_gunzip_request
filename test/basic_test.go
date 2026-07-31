package main_test

import (
	"net/http"
	"testing"

	"github.com/koron/ngx_http_gunzip_request/test/internal/reqsum"
)

func TestBasic(t *testing.T) {
	t.Run("raw", func(t *testing.T) {
		testPost(t, "testdata/40KB.json", false, &reqsum.RequestSummary{
			Method: "POST",
			URL:    "/",
			Proto:  "HTTP/1.1",
			Host:   "127.0.0.1:8081",
			Headers: http.Header{
				"Content-Length": {"40375"},
				"Content-Type":   {"application/json"},
				"User-Agent":     {"Go-http-client/1.1"},
			},
			Body: &reqsum.Body{
				Length: 40375,
				Digests: reqsum.Digests{
					MD5:  "fc594bf63ae918046807dba16ec5aef1",
					SHA1: "b67dd515238d12a4c0aa57bb38954297b4105a49",
				},
			},
		})
	})

	t.Run("gzip without encoding", func(t *testing.T) {
		testPost(t, "testdata/40KB.json.gz", false, &reqsum.RequestSummary{
			Method: "POST",
			URL:    "/",
			Proto:  "HTTP/1.1",
			Host:   "127.0.0.1:8081",
			Headers: http.Header{
				"Content-Length": {"10347"},
				"Content-Type":   {"application/json"},
				"User-Agent":     {"Go-http-client/1.1"},
			},
			Body: &reqsum.Body{
				Length: 10347,
				Digests: reqsum.Digests{
					MD5:  "fd7b83eaedd14cca6f3fe099211ab1a6",
					SHA1: "de1f1ad1da87c482604c46c07f3367a900af2344",
				},
			},
		})
	})

	t.Run("gzip with encoding", func(t *testing.T) {
		testPost(t, "testdata/40KB.json.gz", true, &reqsum.RequestSummary{
			Method: "POST",
			URL:    "/",
			Proto:  "HTTP/1.1",
			Host:   "127.0.0.1:8081",
			Headers: http.Header{
				"Content-Encoding": {"identity"},
				"Content-Length":   {"40375"},
				"Content-Type":     {"application/json"},
				"User-Agent":       {"Go-http-client/1.1"},
			},
			Body: &reqsum.Body{
				Length: 40375,
				Digests: reqsum.Digests{
					MD5:  "fc594bf63ae918046807dba16ec5aef1",
					SHA1: "b67dd515238d12a4c0aa57bb38954297b4105a49",
				},
			},
		})
	})
}

package main

import (
	"log"
	"net/http"

	"github.com/koron/ngx_http_gunzip_request/test/internal/reqsum"
)

func main() {
	err := http.ListenAndServe("localhost:8081", http.HandlerFunc(reqsum.ServeHTTP))
	if err != nil {
		log.Fatal(err)
	}
}

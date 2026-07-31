package reqsum

import (
	"crypto/md5"
	"crypto/sha1"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
)

type RequestSummary struct {
	Method string `json:"method"`
	URL    string `json:"url"`
	Proto  string `json:"proto"`
	Host   string `json:"host,omitempty"`

	Headers http.Header `json:"headers,omitempty"`

	Body *Body `json:"body,omitempty"`
}

type Body struct {
	Length  int64   `json:"length"`
	Digests Digests `json:"digests"`
}

type Digests struct {
	MD5  string `json:"md5"`
	SHA1 string `json:"sha1"`
}

func ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s := RequestSummary{
		Method:  r.Method,
		URL:     r.URL.String(),
		Proto:   r.Proto,
		Host:    r.Host,
		Headers: r.Header,
	}

	if r.ContentLength > 0 {
		md5hash := md5.New()
		sha1hash := sha1.New()
		hw := io.MultiWriter(md5hash, sha1hash)
		n, err := io.Copy(hw, r.Body)
		if err != nil {
			w.WriteHeader(400)
			fmt.Fprintf(w, "hash failure: %s", err)
		}
		s.Body = &Body{
			Length: n,
			Digests: Digests{
				MD5:  hex.EncodeToString(md5hash.Sum(nil)),
				SHA1: hex.EncodeToString(sha1hash.Sum(nil)),
			},
		}
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(200)
	enc := json.NewEncoder(w)
	enc.SetIndent("", "  ")
	enc.Encode(&s)
	fmt.Fprintln(w)
}

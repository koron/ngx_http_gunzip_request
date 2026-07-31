package main_test

import (
	"encoding/json"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/koron/ngx_http_gunzip_request/test/internal/reqsum"
)

const postURL = "http://127.0.0.1:8080/"

var client = &http.Client{
	Transport: &http.Transport{
		DisableCompression: true,
	},
}

func TestMain(m *testing.M) {
	if err := setup(); err != nil {
		panic(err.Error())
	}
	code := m.Run()
	teardown()
	os.Exit(code)
}

func setup() error {
	// Start request summary HTTP server
	go func() {
		err := http.ListenAndServe("localhost:8081", http.HandlerFunc(reqsum.ServeHTTP))
		if err != nil {
			log.Fatalf("request summary server failed: %s", err)
		}
	}()

	// Build and start nginx with the module
	buildCmd := exec.Command("../bin/build")
	if err := buildCmd.Run(); err != nil {
		return err
	}
	nginxCmd := exec.Command("../bin/nginx")
	if err := nginxCmd.Run(); err != nil {
		return err
	}
	return nil
}

func teardown() {
	stopCmd := exec.Command("../bin/nginx", "-s", "stop")
	if err := stopCmd.Run(); err != nil {
		log.Printf("WARN: failed to stop nginx: %s", err)
	}
}

func testPostFile(t *testing.T, filename string, headers ...string) *reqsum.RequestSummary {
	t.Helper()
	f, err := os.Open(filename)
	if err != nil {
		t.Fatalf("failed to open a file to post: %s", err)
	}
	defer f.Close()

	req, err := http.NewRequest("POST", postURL, f)
	if err != nil {
		t.Fatalf("failed to create a request %s: %s", filename, err)
	}
	for i := 0; i+1 < len(headers); i += 2 {
		req.Header.Set(headers[i], headers[i+1])
	}

	resp, err := client.Do(req)
	if err != nil {
		t.Fatalf("failed to post %s: %s", filename, err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		b, err := io.ReadAll(resp.Body)
		if err != nil {
			t.Fatalf("post %s failed with status %d, and failed to read body: %s", filename, resp.StatusCode, err)
		}
		t.Fatalf("post %s failed with status %d body: %q", filename, resp.StatusCode, string(b))
	}
	var sum reqsum.RequestSummary
	err = json.NewDecoder(resp.Body).Decode(&sum)
	if err != nil {
		t.Fatalf("parse RequestSummary failure for %s: %s", filename, err)
	}
	return &sum
}

func testPost(t *testing.T, filename string, withEncoding bool, want *reqsum.RequestSummary) {
	t.Helper()
	opts := []string{"Content-Type", "application/json"}
	if withEncoding {
		opts = append(opts, "Content-Encoding", "gzip")
	}
	got := testPostFile(t, filename, opts...)
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("summary mismatch: -want +got\n%s", d)
	}
}

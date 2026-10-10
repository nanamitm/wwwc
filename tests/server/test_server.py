"""Local HTTP server for the WWWC tests.

Listens only on loopback addresses (127.0.0.1 - 127.0.0.5; WWWC checks one item
per host name at a time, so several addresses let the tests run checks in
parallel). It also works as an HTTP proxy (absolute request URIs).

Every request is appended to the log file as one line:
    <METHOD> <path> auth=<user:pass or None> proxy_auth=<user:pass or None>

Routes:
    /r302...   302 to /target (relative)
    /r301...   301 to http://127.0.0.1:<port>/target (absolute)
    /slow...   waits 30 seconds, then answers like /target
    /drip...   200, sends the body slowly (100 bytes every 0.5 seconds for 30 seconds)
    /title-utf8, /title-sjis, /title-euc, /title-jis
               a Japanese page in that character set
    /filter-sjis
               a long Shift_JIS page for the filter test
    anything else: a small HTML page titled "target page" (67 bytes)

usage: test_server.py <port> <logfile>
"""
import base64
import http.server
import sys
import threading
import time

PORT = int(sys.argv[1])
LOG = sys.argv[2]
ADDRESSES = ["127.0.0.1", "127.0.0.2", "127.0.0.3", "127.0.0.4", "127.0.0.5"]
BODY = b"<html><head><title>target page</title></head><body>ok</body></html>"
lock = threading.Lock()

# /title-<charset>: a page titled TITLE_JA (TITLE_UTF8 for utf8) in that character set
TITLE_JA = "日本語のタイトル"            # 日本語のタイトル
TITLE_UTF8 = TITLE_JA + " \U0001F600 中文 한국어"   # + emoji, Chinese, Korean
def page(title, charset):
    return ("<html><head><title>%s</title></head><body>%s</body></html>" % (title, title)).encode(charset)
TITLE_PAGES = {
    "utf8": page(TITLE_UTF8, "utf-8"),
    "sjis": page(TITLE_JA, "cp932"),
    "euc": page(TITLE_JA, "euc_jp"),
    "jis": page(TITLE_JA, "iso2022_jp"),
}
# /filter-sjis: Shift_JIS page whose UTF-8 form is much longer than the received data
FILTER_PAGE = ("<html><body><div>" + "あ" * 3000 + "</div><p>終わり</p></body></html>").encode("cp932")


def basic(value):
    if not value or not value.startswith("Basic "):
        return None
    raw = base64.b64decode(value[6:])
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("latin-1")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def handle_request(self, send_body):
        with lock, open(LOG, "a", encoding="utf-8") as f:
            f.write("%s %s auth=%r proxy_auth=%r\n" % (
                self.command, self.path,
                basic(self.headers.get("Authorization")),
                basic(self.headers.get("Proxy-Authorization"))))
        path = self.path
        if "://" in path:
            path = "/" + path.split("://", 1)[1].split("/", 1)[-1]
        if path == "/chunked":
            self.protocol_version = "HTTP/1.1"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            if send_body:
                for chunk in (b"abc\n", b"body"):
                    self.wfile.write(b"%x\r\n" % len(chunk) + chunk + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
                self.wfile.flush()
            return
        if path.startswith("/r302"):
            self.send_response(302)
            self.send_header("Location", "/target")
            self.end_headers()
            return
        if path.startswith("/r301"):
            self.send_response(301)
            self.send_header("Location", "http://127.0.0.1:%d/target" % PORT)
            self.end_headers()
            return
        if path.startswith("/title-") and path[7:] in TITLE_PAGES:
            # the same Japanese page in several character sets
            body = TITLE_PAGES[path[7:]]
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if send_body:
                self.wfile.write(body)
            return
        if path.startswith("/filter-sjis"):
            body = FILTER_PAGE
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if send_body:
                self.wfile.write(body)
            return
        if path.startswith("/slow"):
            time.sleep(30)
        if path.startswith("/drip"):
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            for _ in range(60):
                self.wfile.write(b"<p>" + b"x" * 100 + b"</p>\n")
                self.wfile.flush()
                time.sleep(0.5)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(BODY)))
        self.send_header("Last-Modified", "Fri, 09 Oct 2026 08:24:11 GMT")
        self.end_headers()
        if send_body:
            self.wfile.write(BODY)

    def do_GET(self):
        self.handle_request(True)

    def do_HEAD(self):
        self.handle_request(False)

    def log_message(self, *args):
        pass


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        # WWWC aborts connections on cancel and timeout; that is expected
        pass


servers = [Server((a, PORT), Handler) for a in ADDRESSES]
for s in servers[1:]:
    threading.Thread(target=s.serve_forever, daemon=True).start()
servers[0].serve_forever()

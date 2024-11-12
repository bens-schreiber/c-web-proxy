from http.server import SimpleHTTPRequestHandler, HTTPServer
import threading
from typing import Optional


class BasicHTTPRequestHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/plain")
        self.end_headers()
        self.wfile.write(b"you will never work at google!")


def run_server(port: int, stop: Optional[threading.Event] = None):
    server = HTTPServer(("localhost", port), BasicHTTPRequestHandler)
    while stop is None or not stop.is_set():
        server.handle_request()
    server.server_close()


if __name__ == "__main__":
    print("Starting server on port 8080")
    run_server(8080)

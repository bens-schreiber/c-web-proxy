from time import sleep
import pytest
import subprocess
import threading
import requests
from http.server import SimpleHTTPRequestHandler, HTTPServer

SERVER_PORT = 5000
PROXY_PORT = 5001


class BasicHTTPRequestHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/plain")
        self.end_headers()
        self.wfile.write(b"you will never work at google!")


@pytest.fixture(scope="module")
def setup():
    def compile_and_run_proxy() -> subprocess.Popen:
        # Compile the C program
        compile_process = subprocess.run(
            ["gcc", "main.c", "proxy.c", "-o", "test.out"],
            capture_output=True,
            text=True,
        )
        if compile_process.returncode != 0:
            raise RuntimeError(f"Compilation failed: {compile_process.stderr}")

        return subprocess.Popen(
            [f"./test.out", str(PROXY_PORT)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    stop_server = threading.Event()

    def run_basic_http_server():
        server = HTTPServer(("localhost", SERVER_PORT), BasicHTTPRequestHandler)
        while not stop_server.is_set():
            server.handle_request()
        server.server_close()

    # Start the HTTP server in a separate thread
    http_thread = threading.Thread(target=run_basic_http_server, daemon=True)
    http_thread.start()

    # Start the C proxy server
    proxy = compile_and_run_proxy()

    sleep(5)  # MacOS needs to show a dialog to allow the proxy to listen on the port

    yield proxy

    # Terminate the proxy process after tests
    proxy.terminate()
    proxy.wait()
    stop_server.set()


def test_sanity(setup):
    # Arrange
    url = f"http://localhost:{SERVER_PORT}/"

    # Act
    response = requests.get(url)

    # Assert
    assert response.status_code == 200
    assert response.text == "you will never work at google!"


def test_proxy():
    # Arrange
    proxy_url = f"http://localhost:{PROXY_PORT}"
    target_url = f"http://localhost:{SERVER_PORT}"

    proxies = {
        "http": proxy_url,
        "https": proxy_url,
    }

    # Act
    response = requests.get(target_url, proxies=proxies, allow_redirects=True)

    # Assert
    assert response.status_code == 200
    assert response.text == "you will never work at google!"


if __name__ == "__main__":
    pytest.main()

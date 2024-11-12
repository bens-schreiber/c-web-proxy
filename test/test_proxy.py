from time import sleep
import pytest
import subprocess
import threading
import requests
import os

from http_server import run_server

SERVER_PORT = 5000
PROXY_PORT = 5001

root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


@pytest.fixture(scope="module")
def setup():
    def compile_and_run_proxy() -> subprocess.Popen:
        # Compile the C program
        compile_process = subprocess.run(["make"], cwd=root_dir)
        if compile_process.returncode != 0:
            raise RuntimeError(f"Compilation failed: {compile_process.stderr}")

        return subprocess.Popen(
            [f"../proxy_server", str(PROXY_PORT)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    # Start the server in a separate thread
    stop_server = threading.Event()
    server_thread = threading.Thread(
        target=lambda: run_server(SERVER_PORT, stop_server), daemon=True
    )
    server_thread.start()

    # Start the proxy server
    proxy = compile_and_run_proxy()
    sleep(5)  # MacOS needs to show a dialog to allow the proxy to listen on the port

    yield proxy

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


def test_proxy(setup):
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

# Proxy Tests

## Running the tests (pytest)
```
$ cd test
$ python3 -m venv venv
$ source venv/bin/activate
$ pip install -r requirements.txt

$ python3 test.py
```

## Manual testing

### Start the HTTP server
```
$ cd test
$ python3 -m venv venv
$ source venv/bin/activate
$ pip install -r requirements.txt
$ python3 http_server.py
```

### Start the proxy server
```
$ make
$ ./proxy_server <port>
```

### Send a request to the proxy server
```
$ curl -v --proxy http://localhost:<port> http://localhost:8000
```

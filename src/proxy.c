#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

// Network libraries
#include <arpa/inet.h>
#include <netdb.h>

#ifndef LOGGING
#define LOGGING 1
#endif

#if LOGGING
#define LOG(...) printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

// inet and netdb use -1 to indicate an error
#define CATCH_NET_ERROR(out) if (out == -1)

#define READ_TIMEOUT_SEC 3
#define LOOP while (1)
#define MAX_IN_SERVER_QUEUE 10

#define ipv4_socket() socket(AF_INET, SOCK_STREAM, 0)
#define block_until_connection_established(fd) accept(fd, NULL, NULL)

typedef char buffer_t[0xFFF];
typedef char domain_t[0xFF];
typedef char port_t[6]; // max [99999]

ssize_t block_until_read(int client_fd, char *buffer, size_t buffer_size)
{
    fd_set read_fds;
    struct timeval timeout;
    int retval;

    // Initialize the set of active sockets
    FD_ZERO(&read_fds);
    FD_SET(client_fd, &read_fds);

    // Set the timeout
    timeout.tv_sec = READ_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    // Wait for data to be available on the socket
    retval = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (retval == -1)
    {
        // Error occurred in select
        perror("select");
        return -1;
    }
    else if (retval == 0)
    {
        // Timeout occurred
        fprintf(stderr, "Timeout occurred! No data after %d seconds.\n", READ_TIMEOUT_SEC);
        return -1;
    }
    else
    {
        // Data is available, read from the socket
        return read(client_fd, buffer, buffer_size);
    }
}

/// @brief Forward the HTTP request to the remote server.
static void _proxy_request(int client_fd, const domain_t domain, const port_t port, buffer_t buffer);

/// @brief Extract the domain and port from the buffer
/// @remark A default port of 80 is used if not specified
/// @return 1 if the buffer holds a valid GET request and the domain and port were extracted, 0 otherwise
static int _write_domain_port(domain_t domain, port_t port, buffer_t buffer);

/// @brief Write a proxied HTTP GET request to the domain to the buffer
/// @remarks Assumes a valid HTTP GET request is in the buffer
/// @return The length of the request
static int _write_proxy_request(buffer_t buffer, const domain_t domain);

static void _on_connection(int client_fd);

void serve(uint16_t port)
{
#pragma region CREATE_SERVER
    struct sockaddr_in address;
    address.sin_family = AF_INET;         // IPv4
    address.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces, accept connections directed to any of the host’s IP addresses.
    address.sin_port = htons(port);       // Convert the port number to network byte order (big-endian)

    int server_fd = ipv4_socket();
    CATCH_NET_ERROR(server_fd)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    // options: Allow the socket to be reused immediately after it is closed (prevents "Address already in use" errors)
    CATCH_NET_ERROR(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Assign a local address to the socket
    CATCH_NET_ERROR(bind(server_fd, (struct sockaddr *)&address, sizeof(address)))
    {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Mark the socket as a passive socket (a server socket, that will be used to accept incoming connections)
    CATCH_NET_ERROR(listen(server_fd, MAX_IN_SERVER_QUEUE))
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);
#pragma endregion CREATE_SERVER

#pragma region LISTEN
    int client_fd;
    LOOP
    {
        client_fd = block_until_connection_established(server_fd);
        CATCH_NET_ERROR(client_fd)
        {
            perror("accept failed");
            continue;
        }

        LOG("Accepted connection\n");
        _on_connection(client_fd);
    }
#pragma endregion LISTEN

    close(server_fd);
}

static void _on_connection(int client_fd)
{
    buffer_t buffer = {0};
    int bytes_read = -1;

    bytes_read = block_until_read(client_fd, buffer, sizeof(buffer_t));
    CATCH_NET_ERROR(bytes_read)
    {
        perror("read failed");
        close(client_fd);
        return;
    }

    LOG("Received %d bytes from client\n", bytes_read);
    LOG("Data received: %.*s\n", bytes_read, buffer);

    buffer[bytes_read] = '\0';

    domain_t domain;
    port_t port;
    if (_write_domain_port(domain, port, buffer))
    {
        _proxy_request(client_fd, domain, port, buffer);
    }

    close(client_fd);
    LOG("Connection closed.\n\n");
}

static int _write_domain_port(domain_t domain, port_t port, buffer_t buffer)
{
    if (strncmp(buffer, "GET http://", 11) != 0)
    {
        return 0;
    }

    char *url_start = buffer + 11;
    char *url_end = strchr(url_start, ' ');

    if (!url_end)
    {
        return 0;
    }

    *url_end = '\0'; // Null-terminate the URL

    // Find the start of the path
    char *path_start = strchr(url_start, '/');
    if (path_start)
    {
        *path_start = '\0'; // Null-terminate the domain
    }

    // Find the port if specified
    char *port_start = strchr(url_start, ':');
    if (port_start)
    {
        *port_start = '\0'; // Null-terminate the domain
        port_start++;       // Move past the ':'
        strncpy(port, port_start, sizeof(port_t) - 1);
        port[sizeof(port_t) - 1] = '\0'; // Ensure null-termination
    }
    else
    {
        // Default port if not specified
        strncpy(port, "80", sizeof(port_t) - 1);
        port[sizeof(port_t) - 1] = '\0'; // Ensure null-termination
    }

    strncpy(domain, url_start, sizeof(domain_t) - 1);
    domain[sizeof(domain_t) - 1] = '\0'; // Ensure null-termination

    return 1;
}

static int _write_proxy_request(buffer_t buffer, const domain_t domain)
{
    char *path_start = strchr(buffer + 11, '/');
    if (!path_start)
    {
        path_start = "/";
    }

    int length = sprintf(buffer, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", path_start, domain);
    return length;
}

static void _proxy_request(int client_fd, const domain_t domain, const port_t port, buffer_t buffer)
{

#pragma region RESOLVE_DOMAIN
    LOG("Domain: %s\n", domain);
    LOG("Port: %s\n", port);

    struct addrinfo hints, *host, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(domain, port, &hints, &host);
    CATCH_NET_ERROR(status)
    {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(status));
        return;
    }

    struct sockaddr_in server_address;
    for (p = host; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {
            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(atoi(port));
            server_address.sin_addr = ((struct sockaddr_in *)p->ai_addr)->sin_addr;
            break;
        }
    }

    if (p == NULL)
    {
        fprintf(stderr, "No valid address found\n");
        freeaddrinfo(host);
        return;
    }

    freeaddrinfo(host);
    LOG("Resolved domain\n");
#pragma endregion RESOLVE_DOMAIN

#pragma region WRITE_HTTP_GET_TO_REMOTE
    int remote_server_fd = ipv4_socket();
    CATCH_NET_ERROR(remote_server_fd)
    {
        perror("socket failed");
        close(client_fd);
        return;
    }

    CATCH_NET_ERROR(connect(remote_server_fd, (struct sockaddr *)&server_address, sizeof(server_address)))
    {
        perror("connect failed");
        close(remote_server_fd);
        return;
    }
    LOG("Connected to remote server\n");

    int request_length = _write_proxy_request(buffer, domain);
    CATCH_NET_ERROR(write(remote_server_fd, buffer, request_length))
    {
        perror("write failed");
        close(remote_server_fd);
        return;
    }

    LOG("Sent %d bytes to remote server\n", request_length);
    LOG("Data sent: %.*s\n", request_length, buffer);
#pragma endregion WRITE_HTTP_GET_TO_REMOTE

#pragma region FORWARD_RESPONSE_TO_CLIENT
    int bytes_received = -1;

    // Write all of the data from read even if it exceeds the buffer size
    while ((bytes_received = block_until_read(remote_server_fd, buffer, sizeof(buffer_t))) > 0)
    {
        CATCH_NET_ERROR(write(client_fd, buffer, bytes_received))
        {
            perror("remote write failed");
            close(remote_server_fd);
            return;
        }
        LOG("Received %d bytes from remote server\n", bytes_received);
        LOG("Data received: %.*s\n", bytes_received, buffer);
    }
    CATCH_NET_ERROR(bytes_received)
    {
        perror("remote read failed");
        close(remote_server_fd);
        return;
    }
#pragma endregion FORWARD_RESPONSE_TO_CLIENT

    close(remote_server_fd);
}

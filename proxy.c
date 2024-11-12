#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef char buffer_t[0x1000];
typedef char domain_t[0x100];
typedef char port_t[6];

#ifndef LOGGING
#define LOGGING 1
#endif

#if LOGGING
#define LOG(...) printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

/// @brief Extract the domain and port from the buffer
/// @param domain the domain to connect to
/// @param port the port to connect to
/// @param buffer the full HTTP request
/// @return 1 if the domain and port were successfully extracted, 0 otherwise
int write_domain_port(domain_t domain, port_t port, buffer_t buffer)
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

/**
 *
 * As per requirements:
 *
 * Always send the following User-Agent header:
 *     User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3)
 *         Gecko/20120305 Firefox/10.0.3
 *
 * Always send the following Connection header: Connection: close
 * Always send the following Proxy-Connection header: Proxy-Connection: close
 */
int write_request(buffer_t buffer, domain_t domain)
{
    char *path_start = strchr(buffer + 11, '/');
    if (!path_start)
    {
        path_start = "/";
    }

    int length = sprintf(buffer, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", path_start, domain);
    return length;
}

#define IF_ERROR(out) if (out == -1)
#define LOOP while (1)
#define MAX_IN_SERVER_QUEUE 10

#define ipv4_socket() socket(AF_INET, SOCK_STREAM, 0)
#define block_until_connection_established(fd) accept(fd, NULL, NULL)

/// @brief Forward the HTTP request to the remote server
/// @param client_fd the client socket file descriptor
/// @param domain domain name of the remote server
/// @param port port number of the remote server
/// @param buffer the full HTTP request
/// @param bytes_read number of bytes read from the client
void proxy_request(int client_fd, domain_t domain, port_t port, buffer_t buffer, int bytes_read);

void handle_connection(int client_fd);

/// @brief Attempt to listen for incoming connections on the specified port
/// @param port The port to listen on
void start_server(uint16_t port)
{
    struct sockaddr_in address;
    address.sin_family = AF_INET;         // IPv4
    address.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces, accept connections directed to any of the host’s IP addresses.
    address.sin_port = htons(port);       // Convert the port number to network byte order (big-endian)

    int server_fd = ipv4_socket();
    IF_ERROR(server_fd)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    // options: Allow the socket to be reused immediately after it is closed (prevents "Address already in use" errors)
    IF_ERROR(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Assign a local address to the socket
    IF_ERROR(bind(server_fd, (struct sockaddr *)&address, sizeof(address)))
    {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Mark the socket as a passive socket (a server socket, that will be used to accept incoming connections)
    IF_ERROR(listen(server_fd, MAX_IN_SERVER_QUEUE))
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    LOG("Server listening on port %d\n", port);

    int client_fd;
    LOOP
    {
        client_fd = block_until_connection_established(server_fd);
        LOG("Accepted connection\n");

        if (client_fd == -1)
        {
            perror("accept failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        handle_connection(client_fd);
    }

    close(server_fd);
}

void handle_connection(int client_fd)
{
    buffer_t buffer;
    int bytes_read;

    // BLOCKING CALL: Read data from the client
    bytes_read = read(client_fd, buffer, sizeof(buffer));

    IF_ERROR(bytes_read)
    {
        perror("read failed");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    LOG("Received %d bytes from client\n", bytes_read);
    LOG("Data received: %.*s\n", bytes_read, buffer);

    buffer[bytes_read] = '\0';

    domain_t domain;
    port_t port;
    if (write_domain_port(domain, port, buffer))
    {
        proxy_request(client_fd, domain, port, buffer, bytes_read);
    }

    close(client_fd);
    LOG("Connection closed.\n\n");
}

void proxy_request(int client_fd, domain_t domain, port_t port, buffer_t buffer, int bytes_read)
{

#pragma region RESOLVE_DOMAIN
    LOG("Domain: %s\n", domain);
    LOG("Port: %s\n", port);

    struct addrinfo hints, *host, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(domain, port, &hints, &host);
    IF_ERROR(status)
    {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(status));
        close(client_fd);
        exit(EXIT_FAILURE);
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
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(host);
    LOG("Resolved domain\n");
#pragma endregion RESOLVE_DOMAIN

    int remote_server_fd = ipv4_socket();
    IF_ERROR(remote_server_fd)
    {
        perror("socket failed");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    IF_ERROR(connect(remote_server_fd, (struct sockaddr *)&server_address, sizeof(server_address)))
    {
        perror("connect failed");
        close(client_fd);
        close(remote_server_fd);
        exit(EXIT_FAILURE);
    }

    LOG("Connected to remote server\n");

    int request_length = write_request(buffer, domain);
    IF_ERROR(write(remote_server_fd, buffer, request_length))
    {
        perror("write failed");
        close(client_fd);
        close(remote_server_fd);
        exit(EXIT_FAILURE);
    }

    LOG("Sent %d bytes to remote server\n", request_length);
    LOG("Data sent: %.*s\n", request_length, buffer);

    int bytes_received = -1;

    // Write all of the data from read even if it exceeds the buffer size
    while ((bytes_received = read(remote_server_fd, buffer, sizeof(buffer_t))) > 0) // BLOCKING CALL: Read data from the remote server
    {
        IF_ERROR(write(client_fd, buffer, bytes_received))
        {
            perror("remote write failed");
            close(client_fd);
            close(remote_server_fd);
            exit(EXIT_FAILURE);
        }
        LOG("Received %d bytes from remote server\n", bytes_received);
        LOG("Data received: %.*s\n", bytes_received, buffer);
    }

    IF_ERROR(bytes_received)
    {
        perror("remote read failed");
        close(client_fd);
        close(remote_server_fd);
        exit(EXIT_FAILURE);
    }

    close(remote_server_fd);
}
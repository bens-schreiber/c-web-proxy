#include <stdio.h>
#include <stdlib.h>

#include "proxy.h"

int main(int argc, char const *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    uint32_t temp_port = strtoul(argv[1], NULL, 10);
    if (temp_port > 65535)
    {
        fprintf(stderr, "Error: Port number must be between 0 and 65535.\n");
        return 1;
    }

    uint16_t port = (uint16_t)temp_port;
    serve(port);

    return 0;
}
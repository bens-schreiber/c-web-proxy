#include <stdint.h>

/// @brief Run an http proxy server on the specified port
/// @param port The port to listen on
/// @remark Blocks indefinitely
extern void serve(uint16_t port);
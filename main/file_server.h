#pragma once
#include "esp_http_server.h"

// Register file server endpoints onto an already-started httpd server.
// GET /dir?dir=A:path        -> HTML directory listing
// GET /download?file=path    -> raw file bytes (supports Range header)
// GET /api/status            -> JSON device status
void file_server_register_endpoints(httpd_handle_t server);

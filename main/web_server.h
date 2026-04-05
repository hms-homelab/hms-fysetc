#pragma once

// Embedded HTTP server with live log viewer and status dashboard.
// Serves an HTML UI on port 80 that polls /api/logs and /api/status.

void web_server_start(void);
void web_server_stop(void);

#ifndef WEB_H
#define WEB_H
#include "mongoose.h"

#endif //WEB_H

// TODO: get that from some config
#define WEBSERVER_PORT 80
#define WEBSERVER_SERVE_DIR "/app/resources/www,/network-device-manager/network/control=/app/resources/www"
#define WEBSOCKET_PORT 3030
// Looking at various logs it seems to be the same for every CC, couldn't find how it is generated.
#define SDCP_MACHINE_BRAND_IDENTIFIER "979d4C788A4a78bC777A870F1A02867A"
#define SDCP_MAX_HANDLERS 20

typedef enum {
    SDCP_CMD_REFRESH_STATUS = 0,
    SDCP_CMD_ATTRIBUTES = 1
} sdcp_cmd_t;

typedef struct {
    int cmd;
    sdcp_event_handler handler;
} sdcp_handler_entry_t;

typedef void (*sdcp_event_handler)(struct mg_connection*, int, mg_ws_message*, void*);

void webserver_start();
void webserver_stop();
void* webserver_task(void* arg);
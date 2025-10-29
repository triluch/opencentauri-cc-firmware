#ifndef WEB_H
#define WEB_H
#include "mongoose.h"

#endif //WEB_H

// TODO: get that from some config
#define WEBSERVER_PORT 80
#define WEBSERVER_SERVE_DIR "/app/resources/www,/network-device-manager/network/control=/app/resources/www,/thumbnail=/user-resource/file_info"
#define WEBSOCKET_PORT 3030
// Looking at various logs it seems to be the same for every CC, couldn't find how it is generated.
#define SDCP_MACHINE_BRAND_IDENTIFIER "979d4C788A4a78bC777A870F1A02867A"
#define SDCP_MAX_HANDLERS 20
#define MAX_UPLOAD_FILENAME_LEN 255
#define TEMP_PART_UPLOAD_FILE_PATH_FORMAT "/user-resource/.upload-part-%d.tmp"
#define TEMP_UPLOAD_FILE_PATH_FORMAT "/user-resource/.upload-file-%s.tmp"

typedef enum {
    SDCP_CMD_REFRESH_STATUS = 0,
    SDCP_CMD_ATTRIBUTES = 1
} sdcp_cmd_t;

typedef enum
{
    SDCP_MACHINE_STATUS_IDLE = 0,  // Idle
    SDCP_MACHINE_STATUS_PRINTING = 1,  // Executing print task
    SDCP_MACHINE_STATUS_FILE_TRANSFERRING = 2,  // File transfer in progress
    SDCP_MACHINE_STATUS_EXPOSURE_TESTING = 3,  // Exposure test in progress
    SDCP_MACHINE_STATUS_DEVICES_TESTING = 4,  //Device self-check in progress
} sdcp_machine_status_t;

typedef enum
{
    SDCP_PRINT_ERROR_NONE = 0, // Normal
    SDCP_PRINT_ERROR_CHECK = 1, // File MD5 Check Failed
    SDCP_PRINT_ERROR_FILEIO = 2, // File Read Failed
    SDCP_PRINT_ERROR_INVLAID_RESOLUTION = 3,  // Resolution Mismatch
    SDCP_PRINT_ERROR_UNKNOWN_FORMAT = 4,  // Format Mismatch
    SDCP_PRINT_ERROR_UNKNOWN_MODEL = 5,  // Machine Model Mismatch
} sdcp_print_error_t;

typedef void (*sdcp_event_handler)(struct mg_connection*, int, mg_ws_message*, void*);

typedef struct {
    int cmd;
    sdcp_event_handler handler;
} sdcp_handler_entry_t;

typedef struct {
    int offset;
    int total_size;
    bool check;
    char s_file_md5[33];
    char uuid[37];
    char filename[MAX_UPLOAD_FILENAME_LEN+1];
    char temp_file_path[64];
} upload_info_t;

typedef struct {
    bool offset;
    bool total_size;
    bool check;
    bool s_file_md5;
    bool uuid;
    bool file;
} upload_info_fieldset_t;


void webserver_start();
void webserver_stop();
void* webserver_task(void* arg);
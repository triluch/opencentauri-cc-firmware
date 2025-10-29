#include "web.h"
#include "mongoose.h"
#include <math.h>
#include <ctype.h>
#include "cJSON.h"
#include "klippy.h"
#include "params.h"
#include "hl_boot.h"
#include "print_stats_c.h"
#include "simplebus.h"
#include "service.h"
#include "utils.h"
#include <stdio.h>
#include <errno.h>

#define LOG_TAG "web"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#include "file_manager.h"
#include "hl_common.h"
#include "log.h"

static struct mg_mgr web_mgr;

struct {
    int count;
    sdcp_handler_entry_t table[SDCP_MAX_HANDLERS];
} sdcp_handlers;

static char mainboard_id[64] = {};
static char response_topic[96] = "sdcp/response/";
cJSON *previous_status = NULL;
cJSON *current_status = NULL;

// machine_info from params.h

srv_state_res_t srv_state_response;
pthread_mutex_t web_srv_state_mutex;

// "Simplebus" made anything but simple, good job Elegoo.
static void srv_state_subscribe_callback(const char *name, void *context, const int msg_id, void *msg,
                                         uint32_t msg_len) {
    if (msg_id == SRV_STATE_MSG_ID_STATE) {
        pthread_mutex_lock(&web_srv_state_mutex);
        simple_bus_request("srv_state", SRV_STATE_SRV_ID_STATE, NULL, &srv_state_response);
        pthread_mutex_unlock(&web_srv_state_mutex);
    }
}

static cJSON *sdcp_build_status() {
    cJSON *root = cJSON_CreateObject();
    cJSON *status = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "Status", status);

    // CurrentStatus
    {
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(0));
        cJSON_AddItemToObject(status, "CurrentStatus", arr);
    }

    cJSON_AddNumberToObject(status, "TimeLapseStatus", 0);
    cJSON_AddNumberToObject(status, "PlatFormType", 0);

    // Temps
    cJSON_AddNumberToObject(status, "TempOfHotbed",
                            srv_state_response.state.heater_state[HEATER_ID_BED].current_temperature);
    cJSON_AddNumberToObject(status, "TempTargetHotbed",
                            srv_state_response.state.heater_state[HEATER_ID_BED].target_temperature);

    cJSON_AddNumberToObject(status, "TempOfNozzle",
                            srv_state_response.state.heater_state[HEATER_ID_EXTRUDER].current_temperature);
    cJSON_AddNumberToObject(status, "TempTargetNozzle",
                            srv_state_response.state.heater_state[HEATER_ID_EXTRUDER].target_temperature);

    cJSON_AddNumberToObject(status, "TempOfBox",
                            srv_state_response.state.heater_state[HEATER_ID_BOX].current_temperature);
    cJSON_AddNumberToObject(status, "TempTargetBox",
                            srv_state_response.state.heater_state[HEATER_ID_BOX].target_temperature);

    // No, not a typo in the name, it's just "quality" software
    cJSON_AddStringToObject(status, "CurrenCoord", "0.00,0.00,0.00");
    // Fan Speeds
    pthread_mutex_lock(&web_srv_state_mutex);
    cJSON *fan_speeds = cJSON_CreateObject();
    cJSON_AddNumberToObject(fan_speeds, "ModelFan", srv_state_response.state.fan_state[FAN_ID_MODEL].value * 100.);
    cJSON_AddNumberToObject(fan_speeds, "AuxiliaryFan",
                            srv_state_response.state.fan_state[FAN_ID_MODEL_HELPER].value * 100.);
    cJSON_AddNumberToObject(fan_speeds, "BoxFan", srv_state_response.state.fan_state[FAN_ID_BOX].value * 100.);
    cJSON_AddItemToObject(status, "CurrentFanSpeed", fan_speeds);
    pthread_mutex_unlock(&web_srv_state_mutex);

    // ZOffset
    cJSON_AddNumberToObject(status, "ZOffset", 0);
    // LightStatus
    cJSON *light_status = cJSON_CreateObject();
    if (Printer::GetInstance()->m_box_led != nullptr) {
        cJSON_AddNumberToObject(light_status, "SecondLight", 1);
    } else {
        cJSON_AddNumberToObject(light_status, "SecondLight", 0);
    }
    cJSON *rgb_light = cJSON_CreateArray();
    cJSON_AddItemToArray(rgb_light, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(rgb_light, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(rgb_light, cJSON_CreateNumber(0));
    cJSON_AddItemToObject(light_status, "RgbLight", rgb_light);
    cJSON_AddItemToObject(status, "LightStatus", light_status);


    if (Printer::GetInstance()->m_print_stats != nullptr) {
        print_stats_t print_status = Printer::GetInstance()->m_print_stats->get_status(get_monotonic(), NULL);
        cJSON *print_info = cJSON_CreateObject();
        cJSON_AddNumberToObject(print_info, "Status", print_status.state);
        cJSON_AddNumberToObject(print_info, "CurrentLayer", print_status.current_layer);
        cJSON_AddNumberToObject(print_info, "TotalLayer", print_status.total_layers);
        cJSON_AddNumberToObject(print_info, "CurrentTicks", print_status.print_duration);
        cJSON_AddNumberToObject(print_info, "TotalTicks", print_status.total_duration);
        cJSON_AddStringToObject(print_info, "Filename", print_status.filename);
        cJSON_AddNumberToObject(print_info, "ErrorNumber", print_status.error_status_r);
        cJSON_AddStringToObject(print_info, "TaskId", print_status.taskid);
        cJSON_AddNumberToObject(print_info, "PrintSpeedPct", 100); // TODO
        cJSON_AddNumberToObject(print_info, "Progress", print_status.progress);
        cJSON_AddItemToObject(status, "PrintInfo", print_info);
    } else {
        cJSON *print_info = cJSON_CreateObject();
        cJSON_AddNumberToObject(print_info, "Status", 0);
        cJSON_AddNumberToObject(print_info, "CurrentLayer", 0);
        cJSON_AddNumberToObject(print_info, "TotalLayer", 0);
        cJSON_AddNumberToObject(print_info, "CurrentTicks", 0);
        cJSON_AddNumberToObject(print_info, "TotalTicks", 0);
        cJSON_AddStringToObject(print_info, "Filename", "");
        cJSON_AddNumberToObject(print_info, "ErrorNumber", 0);
        cJSON_AddStringToObject(print_info, "TaskId", "");
        cJSON_AddNumberToObject(print_info, "PrintSpeedPct", 100);
        cJSON_AddNumberToObject(print_info, "Progress", 0);
        cJSON_AddItemToObject(status, "PrintInfo", print_info);
    }

    // MainboardID / TimeStamp / Topic
    cJSON_AddStringToObject(root, "MainboardID", mainboard_id);
    cJSON_AddNumberToObject(root, "TimeStamp", (double) time(NULL));
    char topic[96];
    snprintf(topic, sizeof(topic), "sdcp/status/%s", mainboard_id);
    cJSON_AddStringToObject(root, "Topic", topic);

    return root;
}


static cJSON *sdcp_create_base_response(const int cmd, mg_ws_message *mg) {
    cJSON *root = cJSON_CreateObject();
    cJSON *root_data, *data_data;

    cJSON_AddStringToObject(root, "Id", SDCP_MACHINE_BRAND_IDENTIFIER);
    cJSON_AddItemToObject(root, "Data", root_data = cJSON_CreateObject());
    cJSON_AddNumberToObject(root_data, "Cmd", cmd);
    cJSON_AddItemToObject(root_data, "Data", data_data = cJSON_CreateObject());
    cJSON_AddStringToObject(root_data, "MainboardID", mainboard_id);
    cJSON_AddNumberToObject(root_data, "TimeStamp", time(NULL));
    cJSON_AddStringToObject(root, "Topic", response_topic);

    char *request_id = mg_json_get_str(mg->data, "$.Data.RequestID");
    if (request_id) {
        cJSON_AddStringToObject(root_data, "RequestID", request_id);
        free(request_id);
    }

    return root;
}

static void sdcp_send_response(mg_connection *c, cJSON *root) {
    char *result = cJSON_Print(root);
    cJSON_Delete(root);
    mg_ws_send(c, result, strlen(result), WEBSOCKET_OP_TEXT);
    free(result);
}

static void sdcp_refresh_status_handler(mg_connection *c, int cmd, mg_ws_message *mg, void *user_data) {
    cJSON *root = sdcp_create_base_response(cmd, mg);
    cJSON *root_data = cJSON_GetObjectItem(root, "Data");
    cJSON *data_data = cJSON_GetObjectItem(root_data, "Data");

    cJSON_AddNumberToObject(data_data, "Ack", 0);
    // TODO: This should send separate status message

    sdcp_send_response(c, root);
}

static void sdcp_attributes_handler(mg_connection *c, const int cmd, mg_ws_message *mg, void *user_data) {
    cJSON *root = sdcp_create_base_response(cmd, mg);
    cJSON *root_data = cJSON_GetObjectItem(root, "Data");
    cJSON *data_data = cJSON_GetObjectItem(root_data, "Data");

    cJSON_AddNumberToObject(data_data, "Ack", 0);
    // TODO: This should send separate attributte message

    sdcp_send_response(c, root);
}

void sdcp_register_handler(const int cmd, const sdcp_event_handler handler) {
    int i = 0;
    // Replace handler if already exists
    for (i = 0; i < sdcp_handlers.count; i++) {
        if (sdcp_handlers.table[i].cmd == cmd) {
            return;
        }
    }
    if (i < SDCP_MAX_HANDLERS) {
        LOG_I("Registering SDCP handler");
        sdcp_handlers.table[i].cmd = cmd;
        sdcp_handlers.table[i].handler = handler;
        sdcp_handlers.count++;
    } else {
        LOG_E("SDCP_MAX_HANDLERS exceeded, can't register more\n");
    }
}

static void handle_sdcp_command(struct mg_connection *c, const int cmd, mg_ws_message *mg, void *user_data) {
    for (int i = 0; i < sdcp_handlers.count; i++) {
        if (sdcp_handlers.table[i].cmd == cmd) {
            sdcp_handlers.table[i].handler(c, cmd, mg, user_data);
            return;
        }
    }
    LOG_I("SDCP handler not found\n");
    mg_ws_send(c, "{}", 2, WEBSOCKET_OP_TEXT);
}

static cJSON *upload_error_response(const int error_code, const char *error_message, const char *error_field) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "code", "111111");
    cJSON *msg_array = cJSON_CreateArray();
    cJSON *message = cJSON_CreateObject();
    cJSON_AddNumberToObject(message, "message", error_code);
    cJSON_AddStringToObject(message, "field", "common_field");
    cJSON_AddItemToArray(msg_array, message);
    cJSON *custom_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(custom_msg, "message", error_message);
    cJSON_AddStringToObject(custom_msg, "field", error_field);
    cJSON_AddItemToArray(msg_array, custom_msg);
    cJSON_AddItemToObject(root, "messages", msg_array);
    cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    cJSON_AddBoolToObject(root, "success", false);
    return root;
}

static void upload_return_error(struct mg_connection *c, const int status_code, const int error_code,
                                const char *error_message, const char *error_field) {
    cJSON *response = upload_error_response(error_code, error_message, error_field);
    mg_http_reply(c, status_code, "", cJSON_Print(response));
    cJSON_Delete(response);
}

/*
 * Process multipart upload info from HTTP request to extract upload parameters and save uploaded chunk in temp
 * file.
 * TODO: error codes are mostly made up for now, not what Elegoo returns.
 * TODO: probably needs some more verification/error handling. We also ignore "Check" flag and always check MD5.
 *
 */
static void process_upload_info(struct mg_connection *c, const mg_http_message *hm_p, upload_info_t *upload_info_p,
                                upload_info_fieldset_t *upload_info_fs) {
    mg_http_part part;
    size_t ofs = 0;

    while ((ofs = mg_http_next_multipart(hm_p->body, ofs, &part)) > 0) {
        char name_buf[64] = {};
        memcpy(name_buf, part.name.ptr, min((int)part.name.len, 63));
        name_buf[part.name.len] = '\0';
        LOG_I(name_buf, "\n");
        if (strcmp(name_buf, "Offset") == 0) {
            upload_info_p->offset = atoi(std::string(part.body.ptr, part.body.len).c_str());
            upload_info_fs->offset = true;
        }
        if (strcmp(name_buf, "TotalSize") == 0) {
            upload_info_p->total_size = atoi(std::string(part.body.ptr, part.body.len).c_str());
            upload_info_fs->total_size = true;
        }
        if (strcmp(name_buf, "S-File-MD5") == 0) {
            memcpy(upload_info_p->s_file_md5, part.body.ptr, part.body.len);
            upload_info_p->s_file_md5[part.body.len] = '\0';
            for (int i = 0; upload_info_p->s_file_md5[i]; i++) {
                upload_info_p->s_file_md5[i] = tolower(upload_info_p->s_file_md5[i]);
            }
            LOG_I("S-File-MD5: %s\n", upload_info_p->s_file_md5);
            upload_info_fs->s_file_md5 = true;
        }
        if (strcmp(name_buf, "Uuid") == 0) {
            memcpy(upload_info_p->uuid, part.body.ptr, part.body.len);
            upload_info_p->uuid[part.body.len] = '\0';
            for (int i = 0; i<part.body.len; i++) {
                if (!isxdigit(upload_info_p->uuid[i]) && upload_info_p->uuid[i] != '-') {
                    LOG_E("Invalid character in UUID\n");
                    upload_return_error(c, 400, -2, "Invalid UUID format", "uuid");
                    return;
                }
            }
            LOG_I("Uuid: %s\n", upload_info_p->uuid);
            upload_info_fs->uuid = true;
        }
        if (strcmp(name_buf, "File") == 0) {
            if (part.filename.len > MAX_UPLOAD_FILENAME_LEN) {
                LOG_E("Uploaded filename too long\n");
                upload_return_error(c, 400, -3, "Filename too long", "filename");
                return;
            }
            if (part.filename.len < 1) {
                LOG_E("Uploaded filename empty\n");
                upload_return_error(c, 400, -4, "Filename cannot be empty", "filename");
                return;
            }
            memcpy(upload_info_p->filename, part.filename.ptr, part.filename.len);
            upload_info_p->filename[part.filename.len] = '\0';
            snprintf(upload_info_p->temp_file_path, sizeof(upload_info_p->temp_file_path),
                     TEMP_PART_UPLOAD_FILE_PATH_FORMAT, rand());
            FILE *fp = fopen(upload_info_p->temp_file_path, "wb");
            if (fp == NULL) {
                LOG_E("Failed to open temporary file for writing\n");
                upload_return_error(c, 500, -3, "Internal server error", "file");
                return;
            }
            size_t written = fwrite(part.body.ptr, 1, part.body.len, fp);
            fclose(fp);
            if (written != part.body.len) {
                LOG_E("Failed to write complete file chunk to temporary file\n");
                upload_return_error(c, 500, -3, "Internal server error", "file");
                return;
            }
            upload_info_fs->file = true;
            LOG_I("Receiving file chunk of size %lu at offset %d\n", part.body.len, upload_info_p->offset);
        }
    }
}

/*
 * Process uploaded chunk: concat with previous chunks, check for completion, verify MD5, move to final location.
 * TODO: error codes are mostly made up for now, not what Elegoo returns.
 * TODO: same as process_upload_info, some more error handling would be nice.
 */
static void process_upload_chunk(struct mg_connection *c, upload_info_t upload_info) {
    char temp_file_path[64];

    if (upload_info.offset == 0) {
        LOG_I("Starting new upload for file %s of total size %d\n", upload_info.filename, upload_info.total_size);
        char final_temp_file_path[128];
        snprintf(final_temp_file_path, sizeof(final_temp_file_path), TEMP_UPLOAD_FILE_PATH_FORMAT, upload_info.uuid);
        unlink(final_temp_file_path);
        if (rename(upload_info.temp_file_path, final_temp_file_path)) {
            LOG_E("Failed to rename temporary upload file to final temp file: %s\n", strerror(errno));
            unlink(upload_info.temp_file_path);
            unlink(final_temp_file_path);
            upload_return_error(c, 500, -3, "Internal server error", "file");
            return;
        }
    } else {
        // use existing temp uuid file and append new chunk (fail if doesn't exist, or existing file size != offset)
        char final_temp_file_path[128];
        snprintf(final_temp_file_path, sizeof(final_temp_file_path), TEMP_UPLOAD_FILE_PATH_FORMAT, upload_info.uuid);
        FILE *fp = fopen(final_temp_file_path, "ab");
        if (fp == NULL) {
            LOG_E("Failed to open existing upload file for appending\n");
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 400, -5, "No existing upload found", "file");
            return;
        }
        fseek(fp, 0, SEEK_END);
        long existing_size = ftell(fp);
        if (existing_size != upload_info.offset) {
            LOG_E("Existing upload file size %ld does not match expected offset %d\n", existing_size,
                  upload_info.offset);
            fclose(fp);
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 400, -6, "Offset mismatch", "offset");
            return;
        }
        // read from temp part file and append to final temp file
        FILE *part_fp = fopen(upload_info.temp_file_path, "rb");
        if (part_fp == NULL) {
            LOG_E("Failed to open temporary part file for reading\n");
            fclose(fp);
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 500, -3, "Internal server error", "file");
            return;
        }
        fseek(part_fp, 0, SEEK_END);
        long part_size = ftell(part_fp);
        fseek(part_fp, 0, SEEK_SET);
        char *buffer = (char *) malloc(part_size);
        fread(buffer, 1, part_size, part_fp);
        size_t written = fwrite(buffer, 1, part_size, fp);
        free(buffer);
        fclose(part_fp);
        fclose(fp);
        if (written != part_size) {
            LOG_E("Failed to append complete file chunk to existing upload file\n");
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 500, -3, "Internal server error", "file");
            return;
        }
        unlink(upload_info.temp_file_path);
    }
    // check if upload is complete (total_size == file size and md5 sum checks out)
    char final_temp_file_path[128];
    snprintf(final_temp_file_path, sizeof(final_temp_file_path), TEMP_UPLOAD_FILE_PATH_FORMAT, upload_info.uuid);
    FILE *fp = fopen(final_temp_file_path, "rb");
    if (fp == NULL) {
        LOG_E("Failed to open existing upload file for size check\n");
        upload_return_error(c, 500, -3, "Internal server error", "file");
        return;
    }
    fseek(fp, 0, SEEK_END);
    long existing_size = ftell(fp);
    fclose(fp);
    if (existing_size == upload_info.total_size) {
        LOG_I("Upload complete for file %s, size %ld bytes\n", upload_info.filename, existing_size);
        uint8_t md5_sum[16];
        hl_md5(final_temp_file_path, md5_sum);
        char md5_sum_hex[33] = {};
        for (int i = 0; i < 16; i++) {
            sprintf(&md5_sum_hex[i * 2], "%02x", md5_sum[i]);
        }
        if (strcmp(md5_sum_hex, upload_info.s_file_md5) != 0) {
            LOG_E("MD5 checksum mismatch: calculated %s, expected %s\n", md5_sum_hex, upload_info.s_file_md5);
            upload_return_error(c, 400, -7, "MD5 checksum mismatch", "s_file_md5");
            return;
        }
        // move file to /user-resource/(filename) and call FileManager::GetInstance()->AddFile(filename);
        char final_file_path[128];
        snprintf(final_file_path, sizeof(final_file_path), "/user-resource/%s", upload_info.filename);
        rename(final_temp_file_path, final_file_path);
        if (FileManager::GetInstance()->AddFile(final_file_path) == 0) {
            LOG_I("File %s added to FileManager\n", upload_info.filename);
            mg_http_reply(c, 200, "", "{\"code\":\"000000\",\"messages\":null,\"data\":{},\"success\":true}");
        } else {
            LOG_E("Failed to add file %s to FileManager\n", upload_info.filename);
            upload_return_error(c, 500, -8, "Failed to add file to FileManager", "file");
            return;
        }
    } else {
        LOG_I("Upload in progress for file %s, current size %ld bytes, total size %d bytes\n", upload_info.filename,
              existing_size, upload_info.total_size);
        mg_http_reply(c, 200, "", "{\"code\":\"000000\",\"messages\":null,\"data\":{},\"success\":true}");
    }
}

static void handle_web_request(struct mg_connection *c, const int ev, void *ev_data, void *user_data) {
    if (ev == MG_EV_HTTP_MSG && c->pfn != NULL) {
        const mg_http_message *hm = (mg_http_message *) ev_data;
        // TODO: Probably shouldn't allow this during print
        if (mg_http_match_uri(hm, "/uploadFile/upload")) {
            LOG_I("/uploadFile/upload endpoint hit\n");
            upload_info_t upload_info = {};
            upload_info_fieldset_t upload_info_fieldset = {false, false, false, false, false, false};
            process_upload_info(c, hm, &upload_info, &upload_info_fieldset);

            if (upload_info_fieldset.offset &&
                upload_info_fieldset.total_size &&
                upload_info_fieldset.s_file_md5 &&
                upload_info_fieldset.uuid &&
                upload_info_fieldset.file) {
                process_upload_chunk(c, upload_info);
            } else {
                LOG_E("Missing required upload fields\n");
                if (strlen(upload_info.temp_file_path) > 0) {
                    unlink(upload_info.temp_file_path);
                }
                upload_return_error(c, 400, -2, "Missing required upload fields", "filename");
            }
        } else {
            constexpr mg_http_serve_opts opts = {.root_dir = WEBSERVER_SERVE_DIR};
            mg_http_serve_dir(c, (mg_http_message *) ev_data, &opts);
        }
    }
}


static void handle_ws_request(struct mg_connection *c, const int ev, void *ev_data, void *user_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        if (mg_match(hm->uri, mg_str("/websocket"), NULL)) {
            mg_ws_upgrade(c, hm, NULL);
            c->data[0] = 'W'; // mark as websocket for later broadcasts
        } else {
            mg_http_reply(c, 404, "Content-Type: text/plain", "Not found\n");
        }
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        if (wm->data.ptr && wm->data.len == 4 && strcmp(wm->data.ptr, "ping") == 0) {
            mg_ws_send(c, "pong", 4, WEBSOCKET_OP_TEXT);
        } else if (wm->data.ptr && wm->data.len > 0 && *wm->data.ptr == '{') {
            double cmd_double;
            if (mg_json_get_num(wm->data, "$.Data.Cmd", &cmd_double)) {
                const int cmd_int = (int) lround(cmd_double); // JSON loves floats, yay.
                handle_sdcp_command(c, cmd_int, wm, user_data);
            } else {
                LOG_I("Didn't get valid $.Data.Cmd in JSON msg\n");
            }
        } else {
            LOG_I("Unknown message type received on websocket\n");
            //mg_close_conn(c);
        }
    }
}

void webserver_start() {
    LOG_I("Starting webserver\n");
    char web_listen[30];
    char websocket_listen[30];

    hl_get_chipid(mainboard_id, sizeof(mainboard_id));
    strcat(response_topic, mainboard_id);

    srv_state_subscribe_callback("srv_state", NULL, SRV_STATE_MSG_ID_STATE, NULL, 0);
    simple_bus_subscribe("srv_state", NULL, srv_state_subscribe_callback);

    utils_vfork_system("rm /user-resource/.upload-*.tmp");

    sprintf(web_listen, "http://0.0.0.0:%d", WEBSERVER_PORT);
    sprintf(websocket_listen, "http://0.0.0.0:%d", WEBSOCKET_PORT);

    sdcp_register_handler(SDCP_CMD_REFRESH_STATUS, sdcp_refresh_status_handler);
    sdcp_register_handler(SDCP_CMD_ATTRIBUTES, sdcp_attributes_handler);

    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&web_mgr);
    mg_http_listen(&web_mgr, web_listen, handle_web_request, NULL);
    mg_http_listen(&web_mgr, websocket_listen, handle_ws_request, NULL);
}

void webserver_stop() {
    mg_mgr_free(&web_mgr);
}

inline void poll_webserver(const int ms) {
    mg_mgr_poll(&web_mgr, ms);
}

void *webserver_task(void *arg) {
    for (;;) poll_webserver(1000);
}

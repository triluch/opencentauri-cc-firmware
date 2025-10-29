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
#include "web_upload.h"
#include "hl_common.h"

#define LOG_TAG "web"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
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
    cJSON *fan_speeds = cJSON_CreateObject();
    cJSON_AddNumberToObject(fan_speeds, "ModelFan", srv_state_response.state.fan_state[FAN_ID_MODEL].value * 100.);
    cJSON_AddNumberToObject(fan_speeds, "AuxiliaryFan",
                            srv_state_response.state.fan_state[FAN_ID_MODEL_HELPER].value * 100.);
    cJSON_AddNumberToObject(fan_speeds, "BoxFan", srv_state_response.state.fan_state[FAN_ID_BOX].value * 100.);
    cJSON_AddItemToObject(status, "CurrentFanSpeed", fan_speeds);

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

static void handle_web_request(struct mg_connection *c, const int ev, void *ev_data, void *user_data) {
    if (ev == MG_EV_HTTP_MSG && c->pfn != NULL) {
        const mg_http_message *hm = (mg_http_message *) ev_data;
        // TODO: Probably shouldn't allow this during print
        if (mg_http_match_uri(hm, "/uploadFile/upload")) {
            web_handle_upload(c, hm);
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

    // TODO: better cleanup method
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

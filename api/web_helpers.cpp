#include "cJSON.h"
#include "klippy.h"
#include "print_stats_c.h"
#include "gcode_move.h"
#include "web_helpers.h"

#define LOG_TAG "web_status"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#include "log.h"

double web_helper_get_z_offset() {
    const Printer* printer = Printer::GetInstance();
    double z_offset = 0.0;
    // That's the same calculation as in api_get_z_offset_cb in ui_api.cpp
    if (printer->m_tool_head && printer->m_tool_head->m_kin && printer->m_tool_head->m_kin->m_rails.size() > 2 &&
        printer->m_gcode_move && printer->m_strain_gauge && printer->m_strain_gauge->m_cfg) {
        z_offset = printer->m_tool_head->m_kin->m_rails[2]->m_position_endstop
                   - printer->m_tool_head->m_kin->m_rails[2]->m_position_endstop_extra
                   - printer->m_gcode_move->m_base_position[2]
                   - printer->m_strain_gauge->m_cfg->m_fix_z_offset;
        }
    return z_offset;
}

cJSON* web_helper_get_z_offset_json() {
    double z_offset = web_helper_get_z_offset();
    char z_offset_str[32];
    cJSON *z_offset_obj = cJSON_CreateObject();
    snprintf(z_offset_str, sizeof(z_offset_str), "%.24f", z_offset);
    // Why string and double? Who knows. It's Elegoo.
    cJSON_AddStringToObject(z_offset_obj, "source", z_offset_str);
    cJSON_AddNumberToObject(z_offset_obj, "parsedValue", z_offset);
    return z_offset_obj;
}

cJSON* web_helper_get_print_stats_json() {
    const Printer* printer = Printer::GetInstance();
    cJSON *print_info = cJSON_CreateObject();
    if (printer->m_print_stats) {
        const print_stats_t print_status = printer->m_print_stats->get_status(get_monotonic(), NULL);
        cJSON_AddNumberToObject(print_info, "Status", print_status.state);
        cJSON_AddNumberToObject(print_info, "CurrentLayer", print_status.current_layer);
        cJSON_AddNumberToObject(print_info, "TotalLayer", print_status.total_layers);
        cJSON_AddNumberToObject(print_info, "CurrentTicks", print_status.print_duration);
        cJSON_AddNumberToObject(print_info, "TotalTicks", print_status.total_duration);
        cJSON_AddStringToObject(print_info, "Filename", print_status.filename);
        cJSON_AddNumberToObject(print_info, "ErrorNumber", print_status.error_status_r);
        cJSON_AddStringToObject(print_info, "TaskId", print_status.taskid);
        // TODO: needs verification
        cJSON_AddNumberToObject(print_info, "PrintSpeedPct", printer->m_gcode_move->get_gcode_speed_override()*100.);
        cJSON_AddNumberToObject(print_info, "Progress", print_status.progress);
    } else {
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
    }
    return print_info;
}

cJSON* web_helper_get_current_coords_json() {
    char coord_str[32];
    const vector<double> position = Printer::GetInstance()->m_gcode_move->get_gcode_position();
    if (isfinite(position[0]) && isfinite(position[2]) && isfinite(position[3])) {
        snprintf(coord_str, sizeof(coord_str), "%.2f,%.2f,%.2f", position[0], position[1], position[2]);
    } else {
        snprintf(coord_str, sizeof(coord_str), "0.00,0.00,0.00");
    }
    return cJSON_CreateString(coord_str);
}

cJSON* web_helper_get_led_status_json() {
    cJSON *led_status = cJSON_CreateObject();
    const Printer* printer = Printer::GetInstance();
    vector<color_t> led_state = printer->m_printer_pwmled->m_led->m_led_helpers["led2"]->get_status(0.0);
    if (led_state[0].r > 0.0f || led_state[0].g > 0.0f || led_state[0].b > 0.0f) {
        cJSON_AddNumberToObject(led_status, "SecondLight", 1);
    } else {
        cJSON_AddNumberToObject(led_status, "SecondLight", 0);
    }

    cJSON *rgb_light = cJSON_CreateArray();
    cJSON_AddItemToArray(rgb_light, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(rgb_light, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(rgb_light, cJSON_CreateNumber(0));
    cJSON_AddItemToObject(led_status, "RgbLight", rgb_light);
    return led_status;
}

cJSON* sdcp_build_status(const srv_state_res_t &srv_state_response, const char* mainboard_id) {
    const Printer *printer = Printer::GetInstance();
    cJSON *root = cJSON_CreateObject();
    if (!printer) {
        LOG_E("Printer instance is null\n");
        return root;
    }

    cJSON *status = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "Status", status);

    // CurrentStatus - that's probably not correct approach, as not everything is a print job
    {
        cJSON *arr = cJSON_CreateArray();
        int sdcp_status = 0;
        if (printer->m_print_stats) {
            const print_stats_t print_status = printer->m_print_stats->get_status(get_monotonic(), NULL);
            sdcp_status = print_status.state;
        }
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(sdcp_status));
        cJSON_AddItemToObject(status, "CurrentStatus", arr);
    }

    // TODO
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

    cJSON_AddItemToObject(status, "CurrenCoord", web_helper_get_current_coords_json());

    // Fan Speeds
    cJSON *fan_speeds = cJSON_CreateObject();
    cJSON_AddNumberToObject(fan_speeds, "ModelFan",
                            srv_state_response.state.fan_state[FAN_ID_MODEL].value * 100.);
    cJSON_AddNumberToObject(fan_speeds, "AuxiliaryFan",
                            srv_state_response.state.fan_state[FAN_ID_MODEL_HELPER].value * 100.);
    cJSON_AddNumberToObject(fan_speeds, "BoxFan",
                            srv_state_response.state.fan_state[FAN_ID_BOX].value * 100.);
    cJSON_AddItemToObject(status, "CurrentFanSpeed", fan_speeds);

    // ZOffset
    cJSON_AddItemToObject(status, "ZOffset", web_helper_get_z_offset_json());
    // LightStatus
    cJSON_AddItemToObject(status, "LightStatus", web_helper_get_led_status_json());
    // PrintStats
    cJSON_AddItemToObject(status, "PrintStats", web_helper_get_print_stats_json());

    cJSON_AddStringToObject(root, "MainboardID", mainboard_id);
    // We add 0 as timestamp so we can compare status messages easier and fill it in later.
    cJSON_AddNumberToObject(root, "TimeStamp", 0);
    char topic[96];
    snprintf(topic, sizeof(topic), "sdcp/status/%s", mainboard_id);
    cJSON_AddStringToObject(root, "Topic", topic);
    return root;
}
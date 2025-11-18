#ifndef APP_WEB_HELPERS_H
#define APP_WEB_HELPERS_H
#endif //APP_WEB_HELPERS_H
#include "srv_state.h"

double web_helper_get_z_offset();
cJSON *web_helper_get_z_offset_json();
cJSON *web_helper_get_print_stats_json();
cJSON *web_helper_get_current_coords_json();
cJSON *web_helper_get_led_status_json();
cJSON* sdcp_build_status(const srv_state_res_t &srv_state_response, const char* mainboard_id);
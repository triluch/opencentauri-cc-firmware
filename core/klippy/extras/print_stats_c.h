#ifndef PRINT_STATS_C_H
#define PRINT_STATS_C_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "common.h"
    typedef enum
    {
        PRINT_STATS_STATE_STANDBY = 0,
        PRINT_STATS_STATE_HOMING = 1,      // 归零中
        PRINT_STATS_STATE_DROPPING = 2,    // 下降中(LCD)
        PRINT_STATS_STATUS_EXPOSURING = 3, // 曝光中(LCD)
        PRINT_STATS_STATUS_LIFTING = 4,    // 抬升中(LCD)
        PRINT_STATS_STATE_PAUSEING = 5,    // 暂停中
        PRINT_STATS_STATE_PAUSED = 6,      // 已暂停
        PRINT_STATS_STATE_CANCELLING = 7,  // 取消中
        PRINT_STATS_STATE_CANCELLED = 8,
        PRINT_STATS_STATE_COMPLETED = 9,
        PRINT_STATS_STATUS_FILE_CHECKING = 10,    // 文件检查中
        PRINT_STATS_STATUS_DEVICES_CHECKING = 11, // 设备检查中
        PRINT_STATS_STATE_RESUMING = 12,
        PRINT_STATS_STATE_PRINTING = 13,
        PRINT_STATS_STATE_ERROR = 14,
        PRINT_STATS_STATE_AUTOLEVELING = 15,                // 调平中
        PRINT_STATS_STATE_PREHEATING = 16,                  // 预热中
        PRINT_STATS_STATE_RESONANCE_TESTING = 17,           // 振动测试中
        PRINT_STATS_STATE_PRINT_START = 18,                 // 打印开始
        PRINT_STATS_STATE_AUTOLEVELING_COMPLETED = 19,      // 调平完成
        PRINT_STATS_STATE_PREHEATING_COMPLETED = 20,        // 预热完成
        PRINT_STATS_STATE_HOMING_COMPLETED = 21,            // 归零完成
        PRINT_STATS_STATE_RESONANCE_TESTING_COMPLETED = 22, // 振动测试完成
        PRINT_STATS_STATE_FOREIGN_CAPTURE = 23, // 异物检测中
        PRINT_STATS_STATE_FOREIGN_CAPTURE_COMPLETED = 24, // 异物检测完成
        PRINT_STATS_STATE_CHANGE_FILAMENT_START = 25, // 切换 filament 开始
        PRINT_STATS_STATE_CHANGE_FILAMENT_COMPLETED = 26, // 切换 filament 完成
    } print_stats_state_s;
    typedef enum
    {
        PRUSA_SLICER = 0,
        ANYCUBIC_SLICER,
        CURA_SLICER,
        ORCA_SLICER,
        ELEGOO_SLICER,
        OTHERS_SLICER,
    } slice_type_t;
    typedef struct
    {
        uint32_t total_layers; // 总层数
        uint64_t estimated_time;
        char estimeated_time_str[64];
        char filament_type[64];
        double layer_height;
        int fill_density;
        char brim_type[64];
        int support_material_auto;
        char printer_settings_id[64];
        double filament_retract_length;
        double bed_temperature;
        double temperature;
        double perimeters;
        double perimeter_extrusion_width;
        double travel_speed;
        double perimeter_speed;
        double size_x;
        double size_y;
        double size_z;
        double est_filament_length; // 预计耗材长度
        double est_filament_weight; // 预计耗材重量 wait to do
        int thumbnail_width;
        int thumbnail_heigh;
        slice_type_t slice_type;
    } slice_param_t;
    typedef enum
    {
        /* pause */
        PRINT_CAUSE_OK = 0,              /* < 正常 > */
        PRINT_CAUSE_BED_TEMP_ERROR,      /* < 热床温度异常 > */
        PRINT_CAUSE_EXTRUDER_TEMP_ERROR, /* < 喷头温度异常 > */
        PRINT_CAUSE_SG_FAILED,           /* < 力学传感器异常 > */
        PRINT_CAUSE_SG_OFFLINE,          /* < 力学传感器未接入 > */
        PRINT_CAUSE_FILAMENT_LACK,       /* < 耗材不足 > */
        PRINT_CAUSE_FOREIGN_BODY,        /* < 检测到有异物，请排查 > */
        PRINT_CAUSE_RELEASE_FAILED,      /* < 检测到模型脱落，请确认是否继续打印 > */

        /* stop */
        PRINT_CAUSE_UDISK_REMOVE,          /* < 检测到U盘拔出，已停止打印 > */
        PRINT_CAUSE_LEVEL_FAILED,          /* < 自动调平失败，请排查 > */
        PRINT_CAUSE_RESONANCE_TEST_FAILED, /* < 振动测试失败，请排查 > */
        PRINT_CAUSE_HOME_FAILED_X,         /* < 检测X轴电机异常，已停止打印 > */
        PRINT_CAUSE_HOME_FAILED_Y,         /* < 检测Y轴电机异常，已停止打印 > */
        PRINT_CAUSE_HOME_FAILED_Z,         /* < 检测Z轴电机异常，已停止打印 > */
        PRINT_CAUSE_HOME_FAILED,           /* < 归零失败,请检查电机或限位开关是否正常 > */
        PRINT_CAUSE_PLAT_FAILED,           /* < 检测到平台有模型附着,请清理后重新打印 > */
        PRINT_CAUSE_ERROR,                 /* < 未知打印异常 > */

    } print_status_r_t;
    typedef struct print_stats_tag
    {
        char filename[256];     // Print file name
        char local_task_id[64]; // Local task ID
        char taskid[64];        // Remote task ID
        uint32_t current_layer; // Current printed layer
        uint32_t total_layers;  // Total number of layers (should be obtained from slice parameters, added here for compatibility)
        double total_duration;  // Total print time
        double print_duration;  // Time printed so far
        double filament_used;
        int progress;
        int file_src;
        double print_start_time;     // Print start timestamp (seconds)
        double last_pause_time;      // Last pause timestamp (seconds)
        double last_print_time;      // Last print duration (for power loss recovery, seconds)
        double total_pause_duration; // Total pause time
        double init_duration;
        print_stats_state_s state; // Print sub-state
        slice_param_t slice_param;
        print_status_r_t error_status_r; // Print error reason
        } print_stats_t;
#ifdef __cplusplus
}
#endif
#endif
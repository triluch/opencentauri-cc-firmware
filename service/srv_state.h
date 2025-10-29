#ifndef SRV_STATE_H
#define SRV_STATE_H

// Reported by internal modules (Klippy thread) to the SRV_STATE module for aggregation and persistent storage
enum
{
    SRV_HOME_MSG_ID_STATE,
    SRV_HEATER_MSG_ID_STATE,
    SRV_FAN_MSG_ID_STATE,
};

typedef enum
{
    SRV_STATE_HOME_IDLE = 0,
    SRV_STATE_HOME_BEGIN,
    SRV_STATE_HOME_HOMING,
    SRV_STATE_HOME_END_SUCCESS,
    SRV_STATE_HOME_END_FAILED,
} srv_state_home_state_t;

enum
{
    HEATER_ID_EXTRUDER,
    HEATER_ID_BED,
    HEATER_ID_BOX,
    HEATER_NUMBERS,
};

enum
{
    FAN_ID_MODEL = 0,    // model fan
    FAN_ID_MODEL_HELPER, // model auxiliary fan
    FAN_ID_BOX,          // enclosure fan
    FAN_NUMBERS,
};

typedef struct
{
    double target_temperature;
    double current_temperature;
} srv_state_heater_state_t;

typedef struct
{
    double value;
} srv_state_fan_state_t;

typedef struct
{
    int axis; // X,Y,Z
    srv_state_home_state_t st;
} srv_state_home_msg_t;

typedef struct
{
    int heater_id;
    double target_temperature;
    double current_temperature;
} srv_state_heater_msg_t;

typedef struct
{
    int fan_id;
    double value;
} srv_state_fan_msg_t;

typedef enum
{
    SRV_STATE_MSG_ID_STATE,
} srv_state_msg_id_t;

typedef enum
{
    SRV_STATE_SRV_ID_STATE,
} srv_state_srv_id_t;

typedef struct
{
    srv_state_home_state_t home_state[3]; // X,Y,Z
    srv_state_heater_state_t heater_state[HEATER_NUMBERS];
    srv_state_fan_state_t fan_state[FAN_NUMBERS];

} srv_state_t;

typedef struct
{
    srv_state_t state;
} srv_state_res_t;

void srv_state_init(void);

#endif

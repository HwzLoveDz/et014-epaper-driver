#pragma once
#include "et014.h"
#include "et014_esp32.h"

typedef void (*showcase_log_t)(const char *format,...);
typedef enum { SHOWCASE_IDLE, SHOWCASE_RUNNING, SHOWCASE_COMPLETE,
               SHOWCASE_STOPPED, SHOWCASE_FAILED } showcase_state_t;
typedef struct {
    showcase_state_t state;
    unsigned step, updates, scans, gc4, gu4, a2, cleans;
    unsigned remaining_cooldown_ms;
    int temperature_c;
    et014_err_t result;
} showcase_snapshot_t;

/* Single worker owns run(); only stop(), prepare() and snapshot() are cross-task. */
void showcase_task_init(et014_t *device,et014_esp32_t *port,showcase_log_t log);
void showcase_task_prepare(void);
void showcase_task_stop(void);
showcase_snapshot_t showcase_task_snapshot(void);
bool showcase_task_profile_passed(void);
et014_err_t showcase_task_run(void);

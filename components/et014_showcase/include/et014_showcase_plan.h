/* Finite showcase schedule and accounting. No RTOS, GPIO or panel operations. */
#ifndef ET014_SHOWCASE_PLAN_H
#define ET014_SHOWCASE_PLAN_H
#include "et014_showcase.h"
#ifdef __cplusplus
extern "C" {
#endif

#define ET014_SHOWCASE_MAX_UPDATES 12u
#define ET014_SHOWCASE_STEP_COUNT 12u
#define ET014_SHOWCASE_A2_STEPS 5u
#define ET014_SHOWCASE_MIN_A2_HOLD_MS 3500u
#define ET014_SHOWCASE_REPLAY_COOLDOWN_MS 60000u
#define ET014_SHOWCASE_HOLD_POLL_MS 100u
#define ET014_SHOWCASE_NOMINAL_FRAME_US 20000u
#define ET014_SHOWCASE_FRAME_LIMIT_US 23000u
#define ET014_SHOWCASE_POWER_DELAYS_MS 330u
#define ET014_SHOWCASE_WAVEFORM_SHA256 "48b099be4e104e55052baafb70fac10f2c2fa225e83bd2f67ec18452fa5b7a86"

typedef struct {
    et014_showcase_page_t page;
    unsigned animation_index;
    bool inverted;
    et014_mode_t mode;
    bool force_clean;
    uint32_t hold_after_ms;
    bool use_region;
    et014_rect_t region; /* Native 128x296 coordinates; ignored if !use_region. */
    bool sample_temp;    /* Caller samples once, stores real value for rendering. */
} et014_showcase_step_t;

typedef struct {
    int input_temperature_c, waveform_temperature_c;
    uint32_t update_count, clean_count, temperature_samples;
    uint32_t mode_updates[4]; /* Indexed by ET014_AUTO/GC4/GU4/A2. */
    uint32_t total_hold_ms, maximum_hold_ms;
    uint32_t init_scan_frames, image_scan_frames, total_scan_frames;
    uint64_t total_scan_bytes; /* 30,836 SPI bytes per full 296-gate frame. */
    uint32_t nominal_scan_ms;  /* 50-Hz waveform target, not measured runtime. */
    uint32_t frame_limit_scan_ms; /* Frames x driver's accepted 23-ms limit. */
    uint32_t programmed_power_delay_ms;
    uint32_t nominal_plan_ms; /* Hold + nominal scan + fixed power delays. */
    uint32_t frame_limit_plan_ms; /* Excludes renderer, reset, sensor, SPI setup. */
} et014_showcase_budget_t;

/* Sequence runs once and terminates on the REST page. No built-in repetition,
 * retries or background refresh. Core does one power cycle per update.
 * Motion index 0 is a GC4 empty-track baseline; indices 1..5 create/move the
 * ball exclusively through binary A2 region updates (one appearance, four moves).
 * Hold is measured AFTER completed refresh, so A2 starts are >=3.5 s apart.
 * Renderer metrics MUST stay frozen through the motion steps: all changing
 * pixels must stay inside region. Caller checks cancel at <=100-ms hold slices.
 */
size_t et014_showcase_plan_count(void);
const et014_showcase_step_t *et014_showcase_plan_step(size_t index);
et014_rect_t et014_showcase_motion_region(void);

/* Counts come directly from all 10 buckets of the supplied v4 waveform.
 * Nearest temperature, ties to lower bucket, exactly as the core selects.
 * Temperature must be 0..40 C. A2 regions STILL scan all 296 gates.
 * The budget is an upper bound on scheduled operations (no no-change savings).
 * These quantities describe activity/time, NOT rated lifetime or wear limits.
 * Any actual failure is handled by the caller: stop; never auto-retry.
 */
et014_err_t et014_showcase_plan_budget(int temperature_c, et014_showcase_budget_t *out);
void et014_showcase_plan_worst_case(et014_showcase_budget_t *out);
/* Validate static invariants; useful to refuse startup after schedule edits. */
bool et014_showcase_plan_valid(void);
/* Monotonic milliseconds. No wait/sleep; false on a clock moving backwards. */
bool et014_showcase_replay_allowed(uint64_t now_ms, uint64_t last_finish_ms, bool has_finished);

#ifdef __cplusplus
}
#endif
#endif

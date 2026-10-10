/* Original portable driver API. Waveform data retains its supplier terms. */
#ifndef ET014_H
#define ET014_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ET014_WIDTH 128
#define ET014_HEIGHT 296
#define ET014_STRIDE 32
#define ET014_FRAMEBUFFER_BYTES (ET014_STRIDE * ET014_HEIGHT)
#define ET014_TEMPERATURE_SENSOR (-32768)
#define ET014_VCOM_REFERENCE_MV (-2960)
#define ET014_VERSION "1.1.3"

typedef enum {
    ET014_OK = 0, ET014_ERR_ARGUMENT = -1, ET014_ERR_MEMORY = -2,
    ET014_ERR_IO = -3, ET014_ERR_TIMEOUT = -4, ET014_ERR_ID = -5,
    ET014_ERR_TEMPERATURE = -6, ET014_ERR_STATE = -7,
    ET014_ERR_EXPERIMENTAL = -8, ET014_ERR_NOT_BINARY = -9,
    ET014_ERR_CANCELLED = -10, ET014_ERR_BUSY = -11,
    ET014_ERR_TIMING = -12
} et014_err_t;
typedef enum { ET014_AUTO = 0, ET014_GC4 = 1, ET014_GU4 = 2, ET014_A2 = 3 } et014_mode_t;
typedef enum { ET014_BLACK = 0, ET014_DARK_GRAY = 1, ET014_LIGHT_GRAY = 2, ET014_WHITE = 3 } et014_gray_t;
typedef struct { int x, y, width, height; } et014_rect_t;
typedef struct et014 et014_t;

/* One task owns a device. All callbacks and drawing must be externally serialized.
 * transfer: SPI mode 3, MSB first, one CS assertion per call; rx may be NULL.
 * begin(false): configure pins/SPI, PWR high, RESET low >=5 ms then high >=5 ms;
 * OEI stays low. Do not enable panel HV. begin(true): PWR/RESET low, CS physically
 * held HIGH throughout profiling, ignoring peripheral automatic chip-select.
 * end drives RESET/OEI low, releases SPI, drives remaining outputs low, then
 * lowers PWR using the platform's discharge delay. Afterwards power_low is
 * called unconditionally as a fallback, even if end/transfer failed.
 * now_us is monotonic. All callbacks themselves must return in bounded time.
 * busy returns 0 idle, 1 busy, negative on GPIO read error.
 * yield is optional and occurs BETWEEN frames only, never during scanlines.
 */
typedef struct {
    void *user;
    et014_err_t (*begin)(void *user, bool profile);
    et014_err_t (*end)(void *user);
    et014_err_t (*power_low)(void *user);
    et014_err_t (*transfer)(void *user, const uint8_t *tx, uint8_t *rx, size_t length, uint32_t hz);
    int (*busy)(void *user);
    int64_t (*now_us)(void *user);
    void (*delay_us)(void *user, uint32_t us);
    void (*delay_ms)(void *user, uint32_t ms);
    bool (*cancelled)(void *user);
    void (*yield)(void *user);
} et014_hal_t;

typedef struct {
    int vcom_mv;                  /* Explicit calibration/reference, -4000..-1000. */
    int temperature_c;            /* 0..40 or ET014_TEMPERATURE_SENSOR. */
    bool experimental_modes;      /* GU4/A2 opt-in; device acceptance is scenario-specific. */
    uint32_t busy_timeout_us;      /* Default 10000. */
    uint32_t power_timeout_ms;     /* Default 500. */
    uint32_t operation_timeout_ms; /* Default 15000. */
    uint32_t maximum_frame_us;     /* Default 23000, enforce vendor nominal 50 Hz. */
} et014_config_t;

typedef struct {
    et014_mode_t mode;
    int temperature_c, waveform_temperature_c;
    uint32_t frames, init_frames, image_frames;
    uint32_t minimum_frame_us, maximum_frame_us, late_lines;
    uint64_t scan_us, elapsed_us, transfer_bytes;
    uint32_t changed_pixels;
    et014_rect_t updated;
    bool initialized; /* All INIT frames completed; inspect result/history too. */
    bool skipped, history_valid;
    et014_err_t result, cleanup_error;
} et014_stats_t;

void et014_config_default(et014_config_t *config);
/* Allocates approx 29 KB + small LUT; no HAL side effects until operation calls. */
et014_err_t et014_create(const et014_hal_t *hal, const et014_config_t *config, et014_t **out);
void et014_destroy(et014_t *device);
const char *et014_error_string(et014_err_t error);
et014_err_t et014_set_temperature(et014_t *device, int temperature_c);
et014_err_t et014_set_experimental(et014_t *device, bool enabled);

/* Packed 2 bpp, native 128x296, four MSB-first pixels/byte. Gray 0=black..3=white.
 * Raw current buffer is mutable only while idle. Dirty detection compares bytes;
 * no manual mark-dirty is required. Never modify while refresh is executing.
 */
uint8_t *et014_framebuffer(et014_t *device);
const uint8_t *et014_previous_framebuffer(const et014_t *device);
et014_err_t et014_set_pixel(et014_t *device, int x, int y, uint8_t gray);
uint8_t et014_get_pixel(const et014_t *device, int x, int y); /* invalid => white */
et014_err_t et014_fill(et014_t *device, uint8_t gray);
bool et014_history_valid(const et014_t *device);
void et014_invalidate_history(et014_t *device);
bool et014_dirty_bounds(const et014_t *device, et014_rect_t *bounds);
const et014_stats_t *et014_stats(const et014_t *device);
et014_err_t et014_last_error(const et014_t *device);

/* Each operation owns begin/end and always finishes with power_low.
 * probe/temperature only access logic and temperature sensor, never panel HV.
 */
et014_err_t et014_probe(et014_t *device, uint8_t *id);
et014_err_t et014_read_temperature(et014_t *device, int *temperature_c);
/* CS-high throughput profile; caller's begin(true) must implement this contract. */
et014_err_t et014_profile(et014_t *device, unsigned frame_count);

/* Synchronous, bounded. Use an owning worker task for async application use.
 * AUTO currently chooses GC4. NULL rect means full image. Rectangle is strictly
 * bounded; areas outside it keep the committed old image and are not committed.
 * Unknown history in AUTO/GC4 or force_clean performs INIT+GC4 of the ENTIRE
 * target image, because INIT physically clears all pixels. Unknown-history
 * GU4/A2 returns ERR_STATE unless force_clean explicitly chooses INIT+GC4.
 * No changes => no power-on. Every update scans all 296 gates: a small rectangle
 * does not mean less waveform time. Failure/cancellation invalidates history.
 * GU4/A2 require experimental opt-in; A2 requires both complete old/new images
 * binary (0/3), plus known history. They are not promised ghost-free.
 */
et014_err_t et014_refresh(et014_t *device, et014_mode_t mode, const et014_rect_t *rect, bool force_clean);

/* Diagnostic: render current buffer using GC4, THEN the supplied binary target
 * using A2 in the SAME bounded power session (supplier marquee example order).
 * Experimental opt-in required. No electrical/timing/waveform compensation.
 * This tests whether power-cycle boundaries caused the rejected A2 result; it
 * does not claim a fix. next_bytes must be exactly FRAMEBUFFER_BYTES; snapshot
 * copied before power-on (extra 9472-byte allocation). Current buffer becomes
 * next after first GC4 succeeds. Any failure invalidates panel history.
 */
et014_err_t et014_a2_pair(et014_t *device, const uint8_t *next, size_t next_bytes, bool force_clean);

#ifdef __cplusplus
}
#endif
#endif

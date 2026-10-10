/* Portable ET014/GDEP014 software TCON. Original implementation; supplier
 * waveform ownership is recorded in et014_waveform_data.h and project docs.
 * Protocol and waveform timing trace the accepted v0.6 bringup and supplier
 * SWTCON v4, rather than generic SSD16xx command assumptions. */
#include "et014.h"
#include "et014_waveform_data.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define WRITE_HZ 20000000u
#define READ_HZ 500000u
#define LINE_US 68u
#define PACKET_BYTES 49u
#define WF_BUCKETS 10u
#define WF_MODES 4u

struct et014 {
    et014_hal_t hal;
    et014_config_t config;
    et014_stats_t stats;
    et014_err_t error;
    bool active, history, profile_active;
    uint8_t current[ET014_FRAMEBUFFER_BYTES];
    uint8_t previous[ET014_FRAMEBUFFER_BYTES];
    uint8_t source[ET014_FRAMEBUFFER_BYTES];
    uint8_t packet[PACKET_BYTES];
    uint8_t pair_lut[256];
    int64_t line_deadline, operation_deadline;
};

typedef struct {
    int temperature;
    unsigned init_phase_frames, init_pairs;
    unsigned frames[4];
    const uint8_t *data[4];
} waveform_t;

static const et014_rect_t full_rect = {0, 0, ET014_WIDTH, ET014_HEIGHT};
static const uint8_t output_trigger[3] = {0x70, 2, 0x0f};

static unsigned read_u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t read_u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool config_valid(const et014_config_t *c)
{
    return c && c->vcom_mv >= -4000 && c->vcom_mv <= -1000 &&
        (c->temperature_c == ET014_TEMPERATURE_SENSOR || (c->temperature_c >= 0 && c->temperature_c <= 40)) &&
        c->busy_timeout_us > 0 && c->busy_timeout_us <= 100000u &&
        c->power_timeout_ms > 0 && c->power_timeout_ms <= 2000u &&
        c->operation_timeout_ms > 0 && c->operation_timeout_ms <= 30000u &&
        c->maximum_frame_us > 0 && c->maximum_frame_us <= 25000u;
}

void et014_config_default(et014_config_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->vcom_mv = ET014_VCOM_REFERENCE_MV;
    c->temperature_c = ET014_TEMPERATURE_SENSOR;
    c->busy_timeout_us = 10000;
    c->power_timeout_ms = 500;
    c->operation_timeout_ms = 15000;
    c->maximum_frame_us = 23000;
}

et014_err_t et014_create(const et014_hal_t *h, const et014_config_t *c, et014_t **out)
{
    if (!out) return ET014_ERR_ARGUMENT;
    *out = NULL;
    if (!h || !h->begin || !h->end || !h->power_low || !h->transfer || !h->busy ||
        !h->now_us || !h->delay_us || !h->delay_ms || !config_valid(c)) return ET014_ERR_ARGUMENT;
    et014_t *d = (et014_t *)calloc(1, sizeof(*d));
    if (!d) return ET014_ERR_MEMORY;
    d->hal = *h;
    d->config = *c;
    memset(d->current, 0xff, sizeof(d->current));
    memset(d->previous, 0xff, sizeof(d->previous));
    *out = d;
    return ET014_OK;
}

void et014_destroy(et014_t *d)
{
    /* Serialized-owner contract: destroying an in-flight context is invalid. */
    if (d && !d->active) free(d);
}

const char *et014_error_string(et014_err_t e)
{
    switch (e) {
    case ET014_OK: return "ok";
    case ET014_ERR_ARGUMENT: return "invalid argument";
    case ET014_ERR_MEMORY: return "allocation failed";
    case ET014_ERR_IO: return "transport/GPIO failure";
    case ET014_ERR_TIMEOUT: return "bounded hardware wait timed out";
    case ET014_ERR_ID: return "panel ID/status response invalid";
    case ET014_ERR_TEMPERATURE: return "temperature outside waveform range 0..40 C";
    case ET014_ERR_STATE: return "known display history required";
    case ET014_ERR_EXPERIMENTAL: return "experimental waveform disabled";
    case ET014_ERR_NOT_BINARY: return "A2 requires black/white old and new image";
    case ET014_ERR_CANCELLED: return "cancelled at frame boundary";
    case ET014_ERR_BUSY: return "device already active";
    case ET014_ERR_TIMING: return "scan frame exceeded timing bound";
    default: return "HAL error";
    }
}

static et014_err_t remember(et014_t *d, et014_err_t e)
{ if (d) { d->error = e; d->stats.result = e; d->stats.history_valid = d->history; } return e; }

et014_err_t et014_set_temperature(et014_t *d, int t)
{
    if (!d || (t != ET014_TEMPERATURE_SENSOR && (t < 0 || t > 40))) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    d->config.temperature_c = t;
    return ET014_OK;
}
et014_err_t et014_set_experimental(et014_t *d, bool enabled)
{ if (!d) return ET014_ERR_ARGUMENT; if (d->active) return ET014_ERR_BUSY; d->config.experimental_modes = enabled; return ET014_OK; }
uint8_t *et014_framebuffer(et014_t *d) { return d && !d->active ? d->current : NULL; }
const uint8_t *et014_previous_framebuffer(const et014_t *d) { return d ? d->previous : NULL; }
bool et014_history_valid(const et014_t *d) { return d && d->history; }
void et014_invalidate_history(et014_t *d) { if (d && !d->active) { d->history = false; d->stats.history_valid = false; } }
const et014_stats_t *et014_stats(const et014_t *d) { return d ? &d->stats : NULL; }
et014_err_t et014_last_error(const et014_t *d) { return d ? d->error : ET014_ERR_ARGUMENT; }

static uint8_t pixel(const uint8_t *p, int x, int y)
{ return (uint8_t)((p[y * ET014_STRIDE + x / 4] >> (6 - 2 * (x & 3))) & 3); }
static void put_pixel(uint8_t *p, int x, int y, uint8_t gray)
{
    unsigned shift = (unsigned)(6 - 2 * (x & 3));
    uint8_t *v = p + y * ET014_STRIDE + x / 4;
    *v = (uint8_t)((*v & ~(3u << shift)) | (unsigned)gray << shift);
}
et014_err_t et014_set_pixel(et014_t *d, int x, int y, uint8_t gray)
{
    if (!d || x < 0 || x >= ET014_WIDTH || y < 0 || y >= ET014_HEIGHT || gray > 3) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    put_pixel(d->current, x, y, gray);
    return ET014_OK;
}
uint8_t et014_get_pixel(const et014_t *d, int x, int y)
{ return d && x >= 0 && x < ET014_WIDTH && y >= 0 && y < ET014_HEIGHT ? pixel(d->current, x, y) : ET014_WHITE; }
et014_err_t et014_fill(et014_t *d, uint8_t gray)
{
    if (!d || gray > 3) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    memset(d->current, gray * 0x55u, sizeof(d->current));
    return ET014_OK;
}

static bool rect_valid(const et014_rect_t *r)
{ return r && r->x >= 0 && r->y >= 0 && r->width > 0 && r->height > 0 && r->x < ET014_WIDTH && r->y < ET014_HEIGHT && r->width <= ET014_WIDTH - r->x && r->height <= ET014_HEIGHT - r->y; }

static uint32_t differences(const et014_t *d, const et014_rect_t *r, et014_rect_t *bounds)
{
    int xmin = ET014_WIDTH, ymin = ET014_HEIGHT, xmax = -1, ymax = -1;
    uint32_t count = 0;
    for (int y = r->y; y < r->y + r->height; ++y)
        for (int x = r->x; x < r->x + r->width; ++x)
            if (!d->history || pixel(d->current, x, y) != pixel(d->previous, x, y)) {
                ++count;
                if (x < xmin) xmin = x;
                if (y < ymin) ymin = y;
                if (x > xmax) xmax = x;
                if (y > ymax) ymax = y;
            }
    if (bounds) { if (count) { bounds->x = xmin; bounds->y = ymin; bounds->width = xmax - xmin + 1; bounds->height = ymax - ymin + 1; } else memset(bounds, 0, sizeof(*bounds)); }
    return count;
}
bool et014_dirty_bounds(const et014_t *d, et014_rect_t *bounds)
{ if (!d || !bounds) return false; return differences(d, &full_rect, bounds) != 0; }

static et014_err_t transfer(et014_t *d, const uint8_t *tx, uint8_t *rx, size_t n, uint32_t hz)
{
    et014_err_t e = d->hal.transfer(d->hal.user, tx, rx, n, hz);
    if (e == ET014_OK) d->stats.transfer_bytes += n;
    return e;
}
static et014_err_t write_reg(et014_t *d, uint8_t reg, uint8_t value)
{ uint8_t tx[3] = {0x70, reg, value}; return transfer(d, tx, NULL, 3, WRITE_HZ); }
static et014_err_t read_status(et014_t *d, uint8_t *value)
{
    const uint8_t select[2] = {0x70, 0x0f}, request[5] = {0x73, 0xff, 0xff, 0xff, 0xff};
    uint8_t rx[5] = {0};
    et014_err_t e = transfer(d, select, NULL, 2, READ_HZ);
    if (!e) e = transfer(d, request, rx, 5, READ_HZ);
    if (!e && (rx[4] == 0xff || rx[1] != rx[4] || rx[2] != rx[4] || rx[3] != rx[4])) e = ET014_ERR_ID;
    if (!e) *value = rx[4];
    return e;
}
static et014_err_t check_id(et014_t *d, uint8_t *id)
{
    const uint8_t tx[3] = {0x71, 0xff, 0xff};
    uint8_t rx[3] = {0};
    et014_err_t e = transfer(d, tx, rx, 3, READ_HZ);
    if (!e) { *id = rx[2]; if (rx[1] != 0x12 || rx[2] != 0x12) e = ET014_ERR_ID; }
    return e;
}
static et014_err_t temperature_logic(et014_t *d, int *t)
{
    et014_err_t e = write_reg(d, 7, 0xd1);
    if (!e) e = write_reg(d, 8, 3);
    if (e) return e;
    d->hal.delay_ms(d->hal.user, 11);
    uint8_t raw = 0;
    e = read_status(d, &raw);
    if (!e) {
        int code = raw & 31;
        *t = code < 30 ? 3 * code - 19 : 70;
        if (*t < 0 || *t > 40) e = ET014_ERR_TEMPERATURE;
    }
    return e;
}
static void first_error(et014_err_t *first, et014_err_t e) { if (!*first && e) *first = e; }

static et014_err_t power_off(et014_t *d, uint8_t stage)
{
    et014_err_t e = ET014_OK;
    /* Descend attempted states only. Never enable HV as part of logic cleanup. */
    if (stage >= 15) { first_error(&e, write_reg(d, 5, 7)); d->hal.delay_ms(d->hal.user, 30); }
    if (stage >= 7) { first_error(&e, write_reg(d, 5, 3)); d->hal.delay_ms(d->hal.user, 30); }
    if (stage >= 3) { first_error(&e, write_reg(d, 5, 1)); d->hal.delay_ms(d->hal.user, 30); }
    if (stage >= 1) {
        first_error(&e, write_reg(d, 0x0b, 0));
        first_error(&e, write_reg(d, 7, 0x40));
        first_error(&e, write_reg(d, 4, 0x80));
        first_error(&e, write_reg(d, 5, 0));
        d->hal.delay_ms(d->hal.user, 30);
    }
    first_error(&e, write_reg(d, 8, 0));
    d->hal.delay_ms(d->hal.user, 30);
    if (stage >= 1) first_error(&e, write_reg(d, 0x0b, 2));
    return e;
}

static et014_err_t power_on(et014_t *d, uint8_t *stage)
{
    et014_err_t e;
#define TRY(expr) do { e = (expr); if (e != ET014_OK) return e; } while (0)
    TRY(write_reg(d, 7, 0xd1)); TRY(write_reg(d, 8, 3));
    const uint8_t config[10] = {0x70, 1, 0, 0, 0, 0, 0, 0xe0, 0, 0x1d};
    TRY(transfer(d, config, NULL, sizeof(config), WRITE_HZ));
    TRY(write_reg(d, 4, 2));
    TRY(write_reg(d, 9, (uint8_t)(((5600 + d->config.vcom_mv) * 255) / 5600)));
    const uint8_t stages[4] = {0, 1, 3, 7};
    for (unsigned i = 0; i < 4; ++i) {
        *stage = stages[i];
        TRY(write_reg(d, 5, *stage));
        d->hal.delay_ms(d->hal.user, i == 3 ? 60 : 30);
    }
    uint8_t status = 0;
    int64_t deadline = d->hal.now_us(d->hal.user) + (int64_t)d->config.power_timeout_ms * 1000;
    do {
        TRY(read_status(d, &status));
        if (status & 0x40) break;
        d->hal.delay_ms(d->hal.user, 10);
    } while (d->hal.now_us(d->hal.user) < deadline);
    if (!(status & 0x40)) return ET014_ERR_TIMEOUT;
    *stage = 15;
    TRY(write_reg(d, 5, 15));
    d->hal.delay_ms(d->hal.user, 30);
    TRY(write_reg(d, 2, 0x0e));
    return ET014_OK;
#undef TRY
}

static void packet_init(uint8_t *p) { memset(p, 0, PACKET_BYTES); p[0] = 0x70; p[1] = 0x0a; p[2] = 0xc0; }
static void clear_gate(uint8_t *p) { memset(p + 8, 0, 4); memset(p + 44, 0, 5); }
static et014_err_t wait_busy(et014_t *d)
{
    if (d->profile_active) return ET014_OK;
    int64_t deadline = d->hal.now_us(d->hal.user) + d->config.busy_timeout_us;
    for (;;) {
        int busy = d->hal.busy(d->hal.user);
        if (busy < 0) return ET014_ERR_IO;
        if (!busy) return ET014_OK;
        if (d->hal.now_us(d->hal.user) >= deadline) return ET014_ERR_TIMEOUT;
    }
}
static void wait_line(et014_t *d)
{ int64_t remain = d->line_deadline - d->hal.now_us(d->hal.user); if (remain > 0) d->hal.delay_us(d->hal.user, (uint32_t)remain); }
static et014_err_t trigger(et014_t *d)
{ et014_err_t e = wait_busy(d); return e ? e : transfer(d, output_trigger, NULL, sizeof(output_trigger), WRITE_HZ); }

static et014_err_t send_frame(et014_t *d)
{
    const int64_t begin = d->hal.now_us(d->hal.user);
    d->line_deadline = 0;
    for (unsigned row = 0; row < ET014_HEIGHT; ++row) {
        clear_gate(d->packet);
        et014_err_t e = transfer(d, d->packet, NULL, PACKET_BYTES, WRITE_HZ);
        if (e) return e;
        if (d->line_deadline && d->hal.now_us(d->hal.user) > d->line_deadline + 2) ++d->stats.late_lines;
        wait_line(d);
        d->line_deadline = d->hal.now_us(d->hal.user) + LINE_US;
        e = trigger(d);
        if (e) return e;
        unsigned gate = ET014_HEIGHT - 1u - row, select = gate & 15u, block = gate >> 4;
        clear_gate(d->packet);
        d->packet[8 + select / 4] = (uint8_t)(0xc0u >> (2 * (select % 4)));
        d->packet[44 + block / 4] = (uint8_t)(0xc0u >> (2 * (block % 4)));
        memcpy(d->packet + 12, d->source + row * ET014_STRIDE, ET014_STRIDE);
        e = transfer(d, d->packet, NULL, PACKET_BYTES, WRITE_HZ);
        if (!e) e = trigger(d);
        if (e) return e;
    }
    wait_line(d);
    et014_err_t e = wait_busy(d);
    uint8_t clear[PACKET_BYTES];
    packet_init(clear);
    if (!e) e = transfer(d, clear, NULL, sizeof(clear), WRITE_HZ);
    if (!e) e = transfer(d, output_trigger, NULL, sizeof(output_trigger), WRITE_HZ);
    if (!e) {
        int64_t elapsed = d->hal.now_us(d->hal.user) - begin;
        if (elapsed < 0 || elapsed > UINT32_MAX) return ET014_ERR_TIMING;
        uint32_t us = (uint32_t)elapsed;
        if (!d->stats.frames || us < d->stats.minimum_frame_us) d->stats.minimum_frame_us = us;
        if (us > d->stats.maximum_frame_us) d->stats.maximum_frame_us = us;
        d->stats.scan_us += us;
        ++d->stats.frames;
        if (us > d->config.maximum_frame_us) return ET014_ERR_TIMING;
    }
    return e;
}

static et014_err_t boundary(et014_t *d)
{
    if (d->hal.cancelled && d->hal.cancelled(d->hal.user)) return ET014_ERR_CANCELLED;
    if (d->hal.now_us(d->hal.user) >= d->operation_deadline) return ET014_ERR_TIMEOUT;
    return ET014_OK;
}
static et014_err_t one_frame(et014_t *d, bool init)
{
    et014_err_t e = boundary(d);
    if (!e) e = send_frame(d);
    if (!e) { if (init) ++d->stats.init_frames; else ++d->stats.image_frames; }
    if (!e && d->hal.yield) d->hal.yield(d->hal.user);
    return e;
}

static et014_err_t select_waveform(int temperature, waveform_t *w)
{
    if (temperature < 0 || temperature > 40) return ET014_ERR_TEMPERATURE;
    const uint8_t *raw = et014_waveform_data;
    if (read_u16(raw) != 4 || read_u32(raw + 4) != sizeof(et014_waveform_data) || read_u16(raw + 8) != WF_BUCKETS || read_u16(raw + 10) != WF_MODES) return ET014_ERR_STATE;
    unsigned best = 0;
    int best_distance = INT_MAX;
    for (unsigned i = 0; i < WF_BUCKETS; ++i) {
        int candidate = (int16_t)read_u16(raw + 0x30 + i * 20);
        int distance = abs(temperature - candidate);
        if (distance < best_distance) { best = i; best_distance = distance; }
    }
    const uint8_t *entry = raw + 0x30 + best * 20;
    w->temperature = (int16_t)read_u16(entry);
    for (unsigned m = 0; m < 4; ++m) {
        uint32_t off = read_u32(entry + 4 + m * 4);
        if (off > sizeof(et014_waveform_data) - 12u) return ET014_ERR_STATE;
        unsigned frames = read_u16(raw + off);
        if (!frames || frames > 222) return ET014_ERR_STATE;
        w->frames[m] = frames;
        w->data[m] = raw + off + 4;
        if (m && 64u * frames > sizeof(et014_waveform_data) - off - 4u) return ET014_ERR_STATE;
    }
    const uint8_t *init = w->data[0];
    w->init_phase_frames = read_u16(init + 4);
    w->init_pairs = read_u16(init + 6);
    if (read_u32(init) != 0xffffffffu || w->init_phase_frames != 20 || w->init_pairs != 3 || w->frames[0] != 121) return ET014_ERR_STATE;
    return ET014_OK;
}

static et014_err_t initialize_image(et014_t *d, const waveform_t *w)
{
    for (unsigned pair = 0; pair < w->init_pairs; ++pair)
        for (unsigned phase = 0; phase < 2; ++phase) {
            memset(d->source, phase ? 0xaa : 0xff, sizeof(d->source));
            for (unsigned f = 0; f < w->init_phase_frames; ++f) {
                et014_err_t e = one_frame(d, true);
                if (e) return e;
            }
        }
    memset(d->source, 0, sizeof(d->source));
    return one_frame(d, true);
}

static uint8_t wave_code(const uint8_t *frame, unsigned old, unsigned now)
{ unsigned state = old * 16u + now; return (uint8_t)((frame[state >> 2] >> ((state & 3u) * 2)) & 3u); }

static void build_lut(et014_t *d, const uint8_t *frame)
{
    for (unsigned old = 0; old < 16; ++old)
        for (unsigned now = 0; now < 16; ++now)
            d->pair_lut[old * 16u + now] = (uint8_t)((wave_code(frame, old >> 2, now >> 2) << 2) | wave_code(frame, old & 3, now & 3));
}

static void make_source(et014_t *d, const et014_rect_t *r, bool old_white)
{
    uint8_t masks[ET014_STRIDE] = {0};
    for (int x = r->x; x < r->x + r->width; ++x) masks[x / 4] |= (uint8_t)(3u << (6 - 2 * (x & 3)));
    for (int y = 0; y < ET014_HEIGHT; ++y) {
        bool within = y >= r->y && y < r->y + r->height;
        for (int b = 0; b < ET014_STRIDE; ++b) {
            unsigned i = (unsigned)(y * ET014_STRIDE + b);
            uint8_t before = old_white ? 0xff : d->previous[i];
            uint8_t mask = within ? masks[b] : 0;
            uint8_t after = (uint8_t)((before & ~mask) | (d->current[i] & mask));
            d->source[i] = (uint8_t)((d->pair_lut[(before & 0xf0) | (after >> 4)] << 4) | d->pair_lut[((before & 15u) << 4) | (after & 15u)]);
        }
    }
}

static et014_err_t update_image(et014_t *d, const waveform_t *w, et014_mode_t mode, const et014_rect_t *r, bool old_white)
{
    for (unsigned f = 0; f < w->frames[mode]; ++f) {
        build_lut(d, w->data[mode] + f * 64u);
        make_source(d, r, old_white);
        et014_err_t e = one_frame(d, false);
        if (e) return e;
    }
    return ET014_OK;
}

static bool binary(const uint8_t *image)
{
    for (unsigned i = 0; i < ET014_FRAMEBUFFER_BYTES; ++i)
        if (((image[i] ^ (image[i] >> 1)) & 0x55) != 0) return false;
    return true;
}
static void commit(et014_t *d, const et014_rect_t *r)
{
    if (r->x == 0 && r->width == ET014_WIDTH) {
        memcpy(d->previous + r->y * ET014_STRIDE, d->current + r->y * ET014_STRIDE, (size_t)r->height * ET014_STRIDE);
    } else for (int y = r->y; y < r->y + r->height; ++y)
        for (int x = r->x; x < r->x + r->width; ++x) put_pixel(d->previous, x, y, pixel(d->current, x, y));
    d->history = true;
}

static void start_stats(et014_t *d)
{
    memset(&d->stats, 0, sizeof(d->stats));
    d->stats.temperature_c = ET014_TEMPERATURE_SENSOR;
    d->stats.waveform_temperature_c = ET014_TEMPERATURE_SENSOR;
    d->stats.history_valid = d->history;
    d->active = true;
    d->line_deadline = 0;
    d->operation_deadline = d->hal.now_us(d->hal.user) + (int64_t)d->config.operation_timeout_ms * 1000;
    packet_init(d->packet);
}

static et014_err_t finish(et014_t *d, et014_err_t result, et014_err_t cleanup, int64_t start)
{
    first_error(&cleanup, d->hal.end(d->hal.user));
    /* HAL end applies the platform's accepted reset/line-low/release order;
     * unconditional independent power-low remains the last safety fallback. */
    first_error(&cleanup, d->hal.power_low(d->hal.user));
    d->stats.cleanup_error = cleanup;
    first_error(&result, cleanup);
    int64_t elapsed = d->hal.now_us(d->hal.user) - start;
    d->stats.elapsed_us = elapsed > 0 ? (uint64_t)elapsed : 0;
    d->active = false;
    d->profile_active = false;
    return remember(d, result);
}

et014_err_t et014_probe(et014_t *d, uint8_t *id)
{
    if (!d || !id) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    *id = 0;
    start_stats(d);
    int64_t start = d->hal.now_us(d->hal.user);
    et014_err_t e = d->hal.begin(d->hal.user, false);
    if (!e) e = check_id(d, id);
    return finish(d, e, ET014_OK, start);
}

et014_err_t et014_read_temperature(et014_t *d, int *temperature)
{
    if (!d || !temperature) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    *temperature = ET014_TEMPERATURE_SENSOR;
    start_stats(d);
    int64_t start = d->hal.now_us(d->hal.user);
    uint8_t id;
    et014_err_t e = d->hal.begin(d->hal.user, false), cleanup = ET014_OK;
    bool begun = e == ET014_OK;
    if (!e) e = check_id(d, &id);
    if (!e) e = temperature_logic(d, temperature);
    d->stats.temperature_c = *temperature;
    if (begun) cleanup = power_off(d, 0);
    return finish(d, e, cleanup, start);
}

et014_err_t et014_profile(et014_t *d, unsigned frame_count)
{
    if (!d || !frame_count || frame_count > 16) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    start_stats(d);
    d->profile_active = true;
    int64_t start = d->hal.now_us(d->hal.user);
    et014_err_t e = d->hal.begin(d->hal.user, true);
    memset(d->source, 0x55, sizeof(d->source));
    for (unsigned i = 0; !e && i < frame_count; ++i) e = one_frame(d, false);
    return finish(d, e, ET014_OK, start);
}

static et014_err_t refresh_session(et014_t *d, et014_mode_t mode, const et014_rect_t *r, bool clean, const uint8_t *pair_target)
{
    start_stats(d);
    d->stats.mode = mode;
    d->stats.updated = *r;
    d->stats.changed_pixels = differences(d, r, NULL);
    const int64_t start = d->hal.now_us(d->hal.user);
    et014_err_t e = d->hal.begin(d->hal.user, false);
    bool begun = e == ET014_OK, frame_attempted = false;
    uint8_t id = 0, stage = 0;
    int temperature = d->config.temperature_c;
    waveform_t w;
    if (!e) e = check_id(d, &id);
    if (!e && temperature == ET014_TEMPERATURE_SENSOR) e = temperature_logic(d, &temperature);
    if (!e) e = select_waveform(temperature, &w);
    d->stats.temperature_c = temperature;
    if (!e) d->stats.waveform_temperature_c = w.temperature;
    if (!e) e = boundary(d);
    if (!e) e = power_on(d, &stage);
    if (!e) {
        frame_attempted = true;
        if (clean) {
            e = initialize_image(d, &w);
            if (!e) d->stats.initialized = true;
        }
        if (!e) e = update_image(d, &w, pair_target ? ET014_GC4 : mode, r, clean);
        if (!e && pair_target) {
            /* Internally committed reference is needed by the second waveform;
             * any subsequent failure still invalidates ALL history. */
            commit(d, &full_rect);
            memcpy(d->current, pair_target, ET014_FRAMEBUFFER_BYTES);
            e = update_image(d, &w, ET014_A2, &full_rect, false);
        }
    }
    et014_err_t cleanup = ET014_OK;
    if (e && frame_attempted && d->hal.busy(d->hal.user) == 0) {
        uint8_t clear[PACKET_BYTES];
        packet_init(clear);
        first_error(&cleanup, transfer(d, clear, NULL, sizeof(clear), WRITE_HZ));
        if (d->hal.busy(d->hal.user) == 0) first_error(&cleanup, transfer(d, output_trigger, NULL, sizeof(output_trigger), WRITE_HZ));
    }
    if (begun) first_error(&cleanup, power_off(d, stage));
    e = finish(d, e, cleanup, start);
    if (!e) commit(d, r); else d->history = false;
    return remember(d, e);
}

et014_err_t et014_refresh(et014_t *d, et014_mode_t mode, const et014_rect_t *rect, bool force_clean)
{
    if (!d) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    if (mode < ET014_AUTO || mode > ET014_A2 || (rect && !rect_valid(rect))) return remember(d, ET014_ERR_ARGUMENT);
    if ((mode == ET014_GU4 || mode == ET014_A2) && !d->config.experimental_modes) return remember(d, ET014_ERR_EXPERIMENTAL);
    if ((mode == ET014_GU4 || mode == ET014_A2) && !d->history && !force_clean) return remember(d, ET014_ERR_STATE);
    bool clean = force_clean || !d->history;
    if (!clean && mode == ET014_A2 && (!binary(d->previous) || !binary(d->current))) return remember(d, ET014_ERR_NOT_BINARY);
    et014_rect_t r = clean || !rect ? full_rect : *rect;
    et014_mode_t effective = clean || mode == ET014_AUTO ? ET014_GC4 : mode;
    if (!clean && !differences(d, &r, NULL)) {
        memset(&d->stats, 0, sizeof(d->stats));
        d->stats.mode = effective;
        d->stats.skipped = true;
        return remember(d, ET014_OK);
    }
    return refresh_session(d, effective, &r, clean, NULL);
}

et014_err_t et014_a2_pair(et014_t *d, const uint8_t *next, size_t bytes, bool force_clean)
{
    if (!d || !next || bytes != ET014_FRAMEBUFFER_BYTES) return ET014_ERR_ARGUMENT;
    if (d->active) return ET014_ERR_BUSY;
    if (!d->config.experimental_modes) return remember(d, ET014_ERR_EXPERIMENTAL);
    if (!binary(d->current) || !binary(next) || (d->history && !binary(d->previous))) return remember(d, ET014_ERR_NOT_BINARY);
    uint8_t *copy = (uint8_t *)malloc(bytes);
    if (!copy) return remember(d, ET014_ERR_MEMORY);
    memcpy(copy, next, bytes);
    et014_err_t e = refresh_session(d, ET014_A2, &full_rect, force_clean || !d->history, copy);
    free(copy);
    return e;
}

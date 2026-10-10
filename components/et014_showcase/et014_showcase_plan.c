#include "et014_showcase_plan.h"
#include <limits.h>
#include <string.h>

/* Hold budget 58,500 ms; 12 updates: GC4=7, GU4=0, A2=5; INIT=2.
 * One finite demonstration, then indefinite passive image retention. */
#define FULL {0, 0, ET014_WIDTH, ET014_HEIGHT}
#define MOTION {20, 20, 66, 256}
static const et014_showcase_step_t plan[] = {
    {ET014_SHOWCASE_DASHBOARD,  0, false, ET014_GC4, true,   8000, false, FULL,   true },
    /* Full-page GU4 ghosted on this sample; use GC4 for every full-page change. */
    {ET014_SHOWCASE_GALLERY,    0, false, ET014_GC4, false,  8000, false, FULL,   false},
    {ET014_SHOWCASE_GALLERY,    0, true,  ET014_GC4, false,  7000, false, FULL,   false},
    {ET014_SHOWCASE_TYPOGRAPHY, 0, false, ET014_GC4, false,  8000, false, FULL,   true },
    {ET014_SHOWCASE_MOTION,     0, false, ET014_GC4, false,  4000, false, FULL,   false},
    {ET014_SHOWCASE_MOTION,     1, false, ET014_A2,  false,  3500, true,  MOTION, false},
    {ET014_SHOWCASE_MOTION,     2, false, ET014_A2,  false,  3500, true,  MOTION, false},
    {ET014_SHOWCASE_MOTION,     3, false, ET014_A2,  false,  3500, true,  MOTION, false},
    {ET014_SHOWCASE_MOTION,     4, false, ET014_A2,  false,  3500, true,  MOTION, false},
    {ET014_SHOWCASE_MOTION,     5, false, ET014_A2,  false,  3500, true,  MOTION, false},
    {ET014_SHOWCASE_DASHBOARD,  0, false, ET014_GC4, false,  6000, false, FULL,   true },
    {ET014_SHOWCASE_REST,       0, false, ET014_GC4, true,      0, false, FULL,   false}
};
#undef FULL
#undef MOTION

/* Extracted frame counts (not synthesized timing), from supplied v4 blob:
 * dir 0x30 + 20*bucket, mode pointers at +4+4*mode, u16 frame count.
 * Every INIT is 20 frames x two phases x three pairs + zero frame = 121.
 * This small manifest does NOT contain/relicense vendor waveform samples. */
typedef struct { int temperature; unsigned init, gc4, gu4, a2; } frame_counts_t;
static const frame_counts_t frames[] = {
    { 2, 121, 101, 101, 71},
    { 5, 121,  61,  61, 51},
    { 8, 121,  46,  46, 31},
    {11, 121,  46,  46, 25},
    {15, 121,  31,  31, 22},
    {20, 121,  31,  31, 15},
    {25, 121,  31,  31, 16},
    {29, 121,  31,  31, 15},
    {32, 121,  31,  31, 15},
    {37, 121,  31,  31, 15}
};

size_t et014_showcase_plan_count(void) { return sizeof(plan) / sizeof(plan[0]); }
const et014_showcase_step_t *et014_showcase_plan_step(size_t index)
{ return index < et014_showcase_plan_count() ? plan + index : NULL; }
et014_rect_t et014_showcase_motion_region(void)
{ const et014_rect_t r = {20, 20, 66, 256}; return r; }
static const frame_counts_t *select_frames(int temperature)
{
    unsigned best = 0;
    int distance = INT_MAX;
    for (unsigned i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
        int diff = temperature - frames[i].temperature;
        if (diff < 0) diff = -diff;
        if (diff < distance) { best = i; distance = diff; }
    }
    return frames + best;
}

et014_err_t et014_showcase_plan_budget(int temperature, et014_showcase_budget_t *out)
{
    if (!out) return ET014_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (temperature < 0 || temperature > 40) return ET014_ERR_TEMPERATURE;
    const frame_counts_t *w = select_frames(temperature);
    out->input_temperature_c = temperature;
    out->waveform_temperature_c = w->temperature;
    for (size_t i = 0; i < et014_showcase_plan_count(); ++i) {
        const et014_showcase_step_t *s = plan + i;
        ++out->update_count;
        ++out->mode_updates[s->mode];
        if (s->sample_temp) ++out->temperature_samples;
        if (s->force_clean) { ++out->clean_count; out->init_scan_frames += w->init; }
        out->image_scan_frames += s->mode == ET014_A2 ? w->a2 : s->mode == ET014_GU4 ? w->gu4 : w->gc4;
        out->total_hold_ms += s->hold_after_ms;
        if (s->hold_after_ms > out->maximum_hold_ms) out->maximum_hold_ms = s->hold_after_ms;
    }
    out->total_scan_frames = out->init_scan_frames + out->image_scan_frames;
    out->total_scan_bytes = (uint64_t)out->total_scan_frames * 30836u;
    out->nominal_scan_ms = out->total_scan_frames * (ET014_SHOWCASE_NOMINAL_FRAME_US / 1000u);
    out->frame_limit_scan_ms = out->total_scan_frames * (ET014_SHOWCASE_FRAME_LIMIT_US / 1000u);
    out->programmed_power_delay_ms = out->update_count * ET014_SHOWCASE_POWER_DELAYS_MS;
    out->nominal_plan_ms = out->total_hold_ms + out->nominal_scan_ms + out->programmed_power_delay_ms;
    out->frame_limit_plan_ms = out->total_hold_ms + out->frame_limit_scan_ms + out->programmed_power_delay_ms;
    return ET014_OK;
}

void et014_showcase_plan_worst_case(et014_showcase_budget_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    for (int temperature = 0; temperature <= 40; ++temperature) {
        et014_showcase_budget_t candidate;
        if (et014_showcase_plan_budget(temperature, &candidate) == ET014_OK && candidate.total_scan_frames > out->total_scan_frames) *out = candidate;
    }
}

bool et014_showcase_plan_valid(void)
{
    if (et014_showcase_plan_count() != ET014_SHOWCASE_STEP_COUNT || et014_showcase_plan_count() > ET014_SHOWCASE_MAX_UPDATES) return false;
    unsigned clean = 0, a2 = 0;
    bool motion_baseline = false, left_motion = false;
    for (size_t i = 0; i < et014_showcase_plan_count(); ++i) {
        const et014_showcase_step_t *s = plan + i;
        if (s->mode < ET014_GC4 || s->mode > ET014_A2 || s->page < ET014_SHOWCASE_DASHBOARD || s->page > ET014_SHOWCASE_REST) return false;
        if (s->force_clean) {
            ++clean;
            if (s->mode != ET014_GC4 || (i != 0 && i + 1 != et014_showcase_plan_count())) return false;
        }
        if (s->page == ET014_SHOWCASE_MOTION && s->mode == ET014_GC4 && s->animation_index == 0 && !s->use_region) motion_baseline = true;
        if (s->mode == ET014_A2) {
            if (!motion_baseline || left_motion || s->page != ET014_SHOWCASE_MOTION || !s->use_region || s->inverted || s->sample_temp || s->animation_index != a2 + 1u || s->hold_after_ms < ET014_SHOWCASE_MIN_A2_HOLD_MS) return false;
            const et014_rect_t r = et014_showcase_motion_region();
            if (s->region.x != r.x || s->region.y != r.y || s->region.width != r.width || s->region.height != r.height) return false;
            ++a2;
        } else if (a2 && s->page != ET014_SHOWCASE_MOTION) left_motion = true;
        if (s->use_region && (s->region.x < 0 || s->region.y < 0 || s->region.width <= 0 || s->region.height <= 0 || s->region.x >= ET014_WIDTH || s->region.y >= ET014_HEIGHT || s->region.width > ET014_WIDTH - s->region.x || s->region.height > ET014_HEIGHT - s->region.y)) return false;
    }
    const et014_showcase_step_t *last = plan + et014_showcase_plan_count() - 1;
    et014_showcase_budget_t b;
    if (et014_showcase_plan_budget(29, &b) != ET014_OK) return false;
    return clean == 2 && a2 == ET014_SHOWCASE_A2_STEPS && left_motion && last->page == ET014_SHOWCASE_REST && last->hold_after_ms == 0 && last->force_clean && b.mode_updates[ET014_GC4] == 7 && b.mode_updates[ET014_GU4] == 0 && b.total_hold_ms == 58500 && b.total_scan_frames == 534;
}

bool et014_showcase_replay_allowed(uint64_t now, uint64_t last_finish, bool has_finished)
{ return !has_finished || (now >= last_finish && now - last_finish >= ET014_SHOWCASE_REPLAY_COOLDOWN_MS); }

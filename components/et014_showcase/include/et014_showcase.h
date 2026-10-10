/* Small portable UI demo; rendering only, never initiates a panel refresh. */
#ifndef ET014_SHOWCASE_H
#define ET014_SHOWCASE_H
#include "et014_gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

#define ET014_SHOWCASE_WIDTH 296
#define ET014_SHOWCASE_HEIGHT 128
typedef enum {
    ET014_SHOWCASE_DASHBOARD = 0,
    ET014_SHOWCASE_GALLERY,
    ET014_SHOWCASE_TYPOGRAPHY,
    ET014_SHOWCASE_MOTION,
    ET014_SHOWCASE_REST,
    ET014_SHOWCASE_PAGE_COUNT
} et014_showcase_page_t;
typedef struct {
    int temperature_c;
    bool temperature_valid;
    uint32_t last_refresh_ms;
    uint32_t counter;         /* Actual demonstration step number, 1..12. */
    unsigned animation_index; /* 0=empty track; 1..5=A2 ball positions/progress. >5 clamps. */
    bool inverted;            /* Invert complete pages; step affects its area only. */
} et014_showcase_metrics_t;

/* Draw a complete 296x128 logical page using ROTATE_90 on native 128x296.
 * Resets clipping. Metrics must reflect actual results; no synthetic telemetry.
 * optional_font is used only for a short Chinese sample on TYPOGRAPHY; NULL
 * gives the built-in original ASCII design. App owns glyph data and font life.
 * All pages use only supplied graphics primitives and original vector artwork.
 * Motion is entirely binary (0/3); other pages demonstrate 0/1/2/3 gray.
 */
et014_err_t et014_showcase_draw(et014_gfx_t *gfx, et014_showcase_page_t page,
    const et014_showcase_metrics_t *metrics, const et014_font_t *optional_font);

/* After drawing MOTION, redraw only its fixed animation rectangle. No footer,
 * header, page number or statistics change. Requires current ROTATE_90.
 * Resets clip before/after drawing. No allocation and no panel I/O.
 */
et014_err_t et014_showcase_motion_step(et014_gfx_t *gfx,
    const et014_showcase_metrics_t *metrics);
et014_rect_t et014_showcase_motion_rect(void);        /* Logical: 20,42,256,66. */
et014_rect_t et014_showcase_motion_native_rect(void); /* Native: 20,20,66,256. */
const char *et014_showcase_page_name(et014_showcase_page_t page);

#ifdef __cplusplus
}
#endif
#endif

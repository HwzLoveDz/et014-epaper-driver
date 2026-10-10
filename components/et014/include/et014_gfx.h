/* Portable graphics. Coordinates are logical and follow the selected rotation. */
#ifndef ET014_GFX_H
#define ET014_GFX_H
#include "et014.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ET014_ROTATE_0 = 0, ET014_ROTATE_90 = 1,
    ET014_ROTATE_180 = 2, ET014_ROTATE_270 = 3
} et014_rotation_t;
typedef enum {
    ET014_DITHER_NONE = 0, ET014_DITHER_BAYER4 = 1,
    ET014_DITHER_FLOYD_STEINBERG = 2
} et014_dither_t;
typedef struct {
    et014_t *device;
    et014_rotation_t rotation;
    et014_rect_t clip;
} et014_gfx_t;

/* No framebuffer allocation. All writes use the driver's checked pixel API.
 * Drawing/reading must be serialized with refresh by the application.
 * Valid off-screen geometry is clipped, never wrapped. Color is 0..3.
 */
et014_err_t et014_gfx_init(et014_gfx_t *gfx, et014_t *device);
et014_err_t et014_gfx_rotation(et014_gfx_t *gfx, et014_rotation_t rotation);
int et014_gfx_width(const et014_gfx_t *gfx);
int et014_gfx_height(const et014_gfx_t *gfx);
void et014_gfx_reset_clip(et014_gfx_t *gfx);
et014_err_t et014_gfx_clip(et014_gfx_t *gfx, int x, int y, int width, int height);
et014_err_t et014_gfx_pixel(et014_gfx_t *gfx, int x, int y, uint8_t gray);
uint8_t et014_gfx_get_pixel(const et014_gfx_t *gfx, int x, int y);
et014_err_t et014_gfx_clear(et014_gfx_t *gfx, uint8_t gray);
et014_err_t et014_gfx_line(et014_gfx_t *gfx, int x0, int y0, int x1, int y1, uint8_t gray);
et014_err_t et014_gfx_rect(et014_gfx_t *gfx, int x, int y, int width, int height, uint8_t gray, bool filled);
/* Radius is 0..32767; circles outside the clip are discarded. */
et014_err_t et014_gfx_circle(et014_gfx_t *gfx, int x, int y, int radius, uint8_t gray, bool filled);
et014_err_t et014_gfx_invert(et014_gfx_t *gfx, int x, int y, int width, int height);
/* A framed progress bar; percent is 0..100. Minimum dimensions 3x3. */
et014_err_t et014_gfx_progress(et014_gfx_t *gfx, int x, int y, int width, int height, unsigned percent, uint8_t foreground, uint8_t background);

/* Row-major packed MSB first; stride is BYTES per row. data_bytes is checked.
 * 1bpp: bit 1 uses foreground, bit 0 uses background; background=-1 is transparent.
 * 2bpp: 00 black, 01 dark gray, 10 light gray, 11 white.
 */
et014_err_t et014_gfx_bitmap1(et014_gfx_t *gfx, int x, int y, int width, int height, const uint8_t *data, size_t data_bytes, size_t stride, uint8_t foreground, int background);
et014_err_t et014_gfx_bitmap2(et014_gfx_t *gfx, int x, int y, int width, int height, const uint8_t *data, size_t data_bytes, size_t stride);
/* gray8: 0 black..255 white. levels is 2 (binary) or 4. Dither applied before
 * clipping/rotation. FS allocates two (width+2)*sizeof(int32_t) error rows;
 * allocation failure returns ET014_ERR_MEMORY before drawing starts.
 */
et014_err_t et014_gfx_gray8(et014_gfx_t *gfx, int x, int y, int width, int height, const uint8_t *data, size_t data_bytes, size_t stride, unsigned levels, et014_dither_t dither);

/* Built-in original 5x7 ASCII font, 6x8 advance. scale is 1..32.
 * Text accepts UTF-8: unsupported codepoints use '?', not byte-wise garbage.
 * Newlines reset to the initial x and advance one line; there is no auto-wrap.
 * Background -1 is transparent. text_size reports advance dimensions.
 */
et014_err_t et014_gfx_text(et014_gfx_t *gfx, int x, int y, const char *utf8, uint8_t foreground, int background, unsigned scale);
et014_err_t et014_gfx_text_size(const char *utf8, unsigned scale, int *width, int *height);

typedef struct {
    const uint8_t *bitmap; /* 1bpp MSB first, caller-owned; valid during draw. */
    size_t data_bytes, stride;
    int width, height, advance, x_offset, y_offset;
} et014_glyph_t;
typedef bool (*et014_glyph_lookup_t)(void *user, uint32_t codepoint, et014_glyph_t *glyph);
/* Application-supplied font callback supports CJK/custom fonts. No built-in CJK
 * data is included. y is the font's line top; offsets position each glyph.
 * Missing glyphs fall back to ASCII '?'. line_height is 1..32767.
 */
typedef struct {
    void *user;
    et014_glyph_lookup_t lookup;
    int line_height;
} et014_font_t;
et014_err_t et014_gfx_text_font(et014_gfx_t *gfx, int x, int y, const char *utf8, const et014_font_t *font, uint8_t foreground, int background, unsigned scale);
et014_err_t et014_gfx_text_size_font(const char *utf8, const et014_font_t *font, unsigned scale, int *width, int *height);

#ifdef __cplusplus
}
#endif
#endif

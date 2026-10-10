#include "et014_gfx.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TRY(expr) do { et014_err_t e_ = (expr); if (e_ != ET014_OK) return e_; } while (0)

/* Original compact 5x7 glyphs, one five-bit row per byte, MSB at the left. */
static const uint8_t ascii_rows[95][7] = {
    {0,0,0,0,0,0,0}, {4,4,4,4,4,0,4}, {10,10,10,0,0,0,0},
    {10,31,10,10,31,10,0}, {4,15,20,14,5,30,4}, {25,26,2,4,8,11,19},
    {12,18,20,8,21,18,13}, {4,4,8,0,0,0,0}, {2,4,8,8,8,4,2},
    {8,4,2,2,2,4,8}, {0,21,14,31,14,21,0}, {0,4,4,31,4,4,0},
    {0,0,0,0,4,4,8}, {0,0,0,31,0,0,0}, {0,0,0,0,0,0,4},
    {1,2,2,4,8,8,16}, {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30}, {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}, {0,4,0,0,4,0,0},
    {0,4,0,0,4,4,8}, {2,4,8,16,8,4,2}, {0,0,31,0,31,0,0},
    {8,4,2,1,2,4,8}, {14,17,1,2,4,0,4}, {14,17,23,21,23,16,14},
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,25,21,19,19,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,17,10,4,4,4}, {31,1,2,4,8,16,31}, {14,8,8,8,8,8,14},
    {16,8,8,4,2,2,1}, {14,2,2,2,2,2,14}, {4,10,17,0,0,0,0},
    {0,0,0,0,0,0,31}, {8,4,2,0,0,0,0}, {0,0,14,1,15,17,15},
    {16,16,30,17,17,17,30}, {0,0,14,17,16,17,14}, {1,1,15,17,17,17,15},
    {0,0,14,17,31,16,14}, {6,9,8,28,8,8,8}, {0,15,17,17,15,1,14},
    {16,16,30,17,17,17,17}, {4,0,12,4,4,4,14}, {2,0,6,2,2,18,12},
    {16,16,18,20,24,20,18}, {12,4,4,4,4,4,14}, {0,0,26,21,21,21,17},
    {0,0,30,17,17,17,17}, {0,0,14,17,17,17,14}, {0,0,30,17,30,16,16},
    {0,0,15,17,15,1,1}, {0,0,22,25,16,16,16}, {0,0,15,16,14,1,30},
    {8,8,28,8,8,9,6}, {0,0,17,17,17,19,13}, {0,0,17,17,17,10,4},
    {0,0,17,17,21,21,10}, {0,0,17,10,4,10,17}, {0,0,17,17,15,1,14},
    {0,0,31,2,4,8,31}, {2,4,4,8,4,4,2}, {4,4,4,4,4,4,4},
    {8,4,4,2,4,4,8}, {0,0,8,21,2,0,0}
};

static bool valid(const et014_gfx_t *g)
{
    return g && g->device && g->rotation >= ET014_ROTATE_0 && g->rotation <= ET014_ROTATE_270;
}
static bool color_valid(const et014_gfx_t *g, uint8_t gray) { return valid(g) && gray <= 3; }
int et014_gfx_width(const et014_gfx_t *g) { return valid(g) ? ((g->rotation & 1) ? ET014_HEIGHT : ET014_WIDTH) : 0; }
int et014_gfx_height(const et014_gfx_t *g) { return valid(g) ? ((g->rotation & 1) ? ET014_WIDTH : ET014_HEIGHT) : 0; }
void et014_gfx_reset_clip(et014_gfx_t *g)
{
    if (valid(g)) { g->clip.x = g->clip.y = 0; g->clip.width = et014_gfx_width(g); g->clip.height = et014_gfx_height(g); }
}
et014_err_t et014_gfx_init(et014_gfx_t *g, et014_t *dev)
{
    if (!g || !dev) return ET014_ERR_ARGUMENT;
    g->device = dev; g->rotation = ET014_ROTATE_0; et014_gfx_reset_clip(g);
    return ET014_OK;
}
et014_err_t et014_gfx_rotation(et014_gfx_t *g, et014_rotation_t rotation)
{
    if (!valid(g) || rotation < ET014_ROTATE_0 || rotation > ET014_ROTATE_270) return ET014_ERR_ARGUMENT;
    g->rotation = rotation; et014_gfx_reset_clip(g); return ET014_OK;
}
static et014_rect_t intersection(int64_t x, int64_t y, int64_t w, int64_t h, et014_rect_t c)
{
    int64_t left = x > c.x ? x : c.x, top = y > c.y ? y : c.y;
    int64_t right = x + w < (int64_t)c.x + c.width ? x + w : (int64_t)c.x + c.width;
    int64_t bottom = y + h < (int64_t)c.y + c.height ? y + h : (int64_t)c.y + c.height;
    et014_rect_t r = {0,0,0,0};
    if (right > left && bottom > top) { r.x = (int)left; r.y = (int)top; r.width = (int)(right-left); r.height = (int)(bottom-top); }
    return r;
}
et014_err_t et014_gfx_clip(et014_gfx_t *g, int x, int y, int w, int h)
{
    if (!valid(g) || w < 0 || h < 0) return ET014_ERR_ARGUMENT;
    et014_rect_t all = {0,0,et014_gfx_width(g),et014_gfx_height(g)};
    g->clip = intersection(x,y,w,h,all); return ET014_OK;
}
static void map(const et014_gfx_t *g, int x, int y, int *nx, int *ny)
{
    switch (g->rotation) {
    default: *nx=x; *ny=y; break;
    case ET014_ROTATE_90: *nx=ET014_WIDTH-1-y; *ny=x; break;
    case ET014_ROTATE_180: *nx=ET014_WIDTH-1-x; *ny=ET014_HEIGHT-1-y; break;
    case ET014_ROTATE_270: *nx=y; *ny=ET014_HEIGHT-1-x; break;
    }
}
static bool in_clip(const et014_gfx_t *g, int x, int y)
{
    return x >= g->clip.x && y >= g->clip.y && (int64_t)x < (int64_t)g->clip.x + g->clip.width && (int64_t)y < (int64_t)g->clip.y + g->clip.height;
}
et014_err_t et014_gfx_pixel(et014_gfx_t *g, int x, int y, uint8_t gray)
{
    if (!color_valid(g,gray)) return ET014_ERR_ARGUMENT;
    if (!in_clip(g,x,y)) return ET014_OK;
    int nx,ny; map(g,x,y,&nx,&ny); return et014_set_pixel(g->device,nx,ny,gray);
}
uint8_t et014_gfx_get_pixel(const et014_gfx_t *g, int x, int y)
{
    if (!valid(g) || x < 0 || y < 0 || x >= et014_gfx_width(g) || y >= et014_gfx_height(g)) return ET014_WHITE;
    int nx,ny; map(g,x,y,&nx,&ny); return et014_get_pixel(g->device,nx,ny);
}
static et014_err_t fill_wide(et014_gfx_t *g, int64_t x, int64_t y, int64_t w, int64_t h, uint8_t gray)
{
    et014_rect_t r=intersection(x,y,w,h,g->clip);
    for (int iy=r.y; iy < r.y+r.height; ++iy) for (int ix=r.x; ix < r.x+r.width; ++ix) TRY(et014_gfx_pixel(g,ix,iy,gray));
    return ET014_OK;
}
et014_err_t et014_gfx_clear(et014_gfx_t *g, uint8_t gray)
{
    if (!color_valid(g,gray)) return ET014_ERR_ARGUMENT;
    return fill_wide(g,0,0,et014_gfx_width(g),et014_gfx_height(g),gray);
}
et014_err_t et014_gfx_rect(et014_gfx_t *g, int x, int y, int w, int h, uint8_t gray, bool filled)
{
    if (!color_valid(g,gray) || w < 0 || h < 0) return ET014_ERR_ARGUMENT;
    if (!w || !h) return ET014_OK;
    if (filled) return fill_wide(g,x,y,w,h,gray);
    TRY(fill_wide(g,x,y,w,1,gray)); TRY(fill_wide(g,x,(int64_t)y+h-1,w,1,gray));
    TRY(fill_wide(g,x,y,1,h,gray)); return fill_wide(g,(int64_t)x+w-1,y,1,h,gray);
}
/* Liang-Barsky clipping bounds work even for extreme INT_MIN/MAX endpoints;
 * floating intermediates avoid overflowing the products of 32-bit deltas. */
static bool clip_edge(double p, double q, double *lo, double *hi)
{
    if (p == 0) return q >= 0;
    double t=q/p;
    if (p < 0) { if (t > *hi) return false; if (t > *lo) *lo=t; }
    else { if (t < *lo) return false; if (t < *hi) *hi=t; }
    return true;
}
static int nearest(double value) { return (int)(value >= 0 ? value+0.5 : value-0.5); }
et014_err_t et014_gfx_line(et014_gfx_t *g, int ax, int ay, int bx, int by, uint8_t gray)
{
    if (!color_valid(g,gray)) return ET014_ERR_ARGUMENT;
    if (!g->clip.width || !g->clip.height) return ET014_OK;
    double dx=(double)bx-ax,dy=(double)by-ay,lo=0,hi=1;
    if (!clip_edge(-dx,(double)ax-g->clip.x,&lo,&hi) || !clip_edge(dx,(double)g->clip.x+g->clip.width-1-ax,&lo,&hi) ||
        !clip_edge(-dy,(double)ay-g->clip.y,&lo,&hi) || !clip_edge(dy,(double)g->clip.y+g->clip.height-1-ay,&lo,&hi)) return ET014_OK;
    int x0=nearest(ax+lo*dx),y0=nearest(ay+lo*dy),x1=nearest(ax+hi*dx),y1=nearest(ay+hi*dy);
    int ix=abs(x1-x0),sx=x0<x1 ? 1:-1,iy=-abs(y1-y0),sy=y0<y1 ? 1:-1,error=ix+iy;
    for (;;) {
        TRY(et014_gfx_pixel(g,x0,y0,gray)); if (x0==x1 && y0==y1) break;
        int twice=2*error; if (twice>=iy) { error+=iy; x0+=sx; } if (twice<=ix) { error+=ix; y0+=sy; }
    }
    return ET014_OK;
}
static et014_err_t point_wide(et014_gfx_t *g,int64_t x,int64_t y,uint8_t gray) { return fill_wide(g,x,y,1,1,gray); }
et014_err_t et014_gfx_circle(et014_gfx_t *g, int cx, int cy, int radius, uint8_t gray, bool filled)
{
    if (!color_valid(g,gray) || radius<0 || radius>32767) return ET014_ERR_ARGUMENT;
    et014_rect_t bounds=intersection((int64_t)cx-radius,(int64_t)cy-radius,2*radius+1,2*radius+1,g->clip);
    if (!bounds.width) return ET014_OK;
    int x=radius,y=0,error=1-radius;
    while (x>=y) {
        if (filled) {
            TRY(fill_wide(g,(int64_t)cx-x,(int64_t)cy+y,2*x+1,1,gray));
            TRY(fill_wide(g,(int64_t)cx-x,(int64_t)cy-y,2*x+1,1,gray));
            TRY(fill_wide(g,(int64_t)cx-y,(int64_t)cy+x,2*y+1,1,gray));
            TRY(fill_wide(g,(int64_t)cx-y,(int64_t)cy-x,2*y+1,1,gray));
        } else {
            TRY(point_wide(g,(int64_t)cx+x,(int64_t)cy+y,gray)); TRY(point_wide(g,(int64_t)cx+y,(int64_t)cy+x,gray));
            TRY(point_wide(g,(int64_t)cx-x,(int64_t)cy+y,gray)); TRY(point_wide(g,(int64_t)cx-y,(int64_t)cy+x,gray));
            TRY(point_wide(g,(int64_t)cx+x,(int64_t)cy-y,gray)); TRY(point_wide(g,(int64_t)cx+y,(int64_t)cy-x,gray));
            TRY(point_wide(g,(int64_t)cx-x,(int64_t)cy-y,gray)); TRY(point_wide(g,(int64_t)cx-y,(int64_t)cy-x,gray));
        }
        ++y; if (error<0) error+=2*y+1; else { --x; error+=2*(y-x)+1; }
    }
    return ET014_OK;
}
et014_err_t et014_gfx_invert(et014_gfx_t *g,int x,int y,int w,int h)
{
    if (!valid(g) || w<0 || h<0) return ET014_ERR_ARGUMENT;
    et014_rect_t r=intersection(x,y,w,h,g->clip);
    for (int iy=r.y;iy<r.y+r.height;++iy) for(int ix=r.x;ix<r.x+r.width;++ix) TRY(et014_gfx_pixel(g,ix,iy,(uint8_t)(3-et014_gfx_get_pixel(g,ix,iy))));
    return ET014_OK;
}
et014_err_t et014_gfx_progress(et014_gfx_t *g,int x,int y,int w,int h,unsigned percent,uint8_t fg,uint8_t bg)
{
    if (!color_valid(g,fg) || bg>3 || percent>100 || w<3 || h<3) return ET014_ERR_ARGUMENT;
    TRY(et014_gfx_rect(g,x,y,w,h,fg,false));
    TRY(fill_wide(g,(int64_t)x+1,(int64_t)y+1,w-2,h-2,bg));
    return fill_wide(g,(int64_t)x+1,(int64_t)y+1,((int64_t)w-2)*percent/100,h-2,fg);
}
static bool buffer_valid(const uint8_t *p,size_t bytes,size_t stride,int w,int h,unsigned bpp)
{
    if (!p || w<=0 || h<=0) return false;
    if((size_t)w>(SIZE_MAX-7)/bpp)return false;
    size_t minimum=((size_t)w*bpp+7)/8;
    return stride>=minimum && (size_t)(h-1)<=(SIZE_MAX-minimum)/stride && (size_t)(h-1)*stride+minimum<=bytes;
}
static et014_err_t bitmap(et014_gfx_t *g,int x,int y,int w,int h,const uint8_t *data,size_t bytes,size_t stride,unsigned bpp,uint8_t fg,int bg)
{
    if (!valid(g) || !buffer_valid(data,bytes,stride,w,h,bpp) || fg>3 || bg < -1 || bg>3) return ET014_ERR_ARGUMENT;
    et014_rect_t r=intersection(x,y,w,h,g->clip);
    for(int iy=r.y;iy<r.y+r.height;++iy) for(int ix=r.x;ix<r.x+r.width;++ix) {
        size_t px=(size_t)((int64_t)ix-x),py=(size_t)((int64_t)iy-y);
        uint8_t byte=data[py*stride+px/(8/bpp)],value=(uint8_t)((byte>>(8-bpp-(px%(8/bpp))*bpp))&((1u<<bpp)-1));
        int gray=bpp==2 ? value : (value ? fg:bg);
        if(gray>=0) TRY(et014_gfx_pixel(g,ix,iy,(uint8_t)gray));
    }
    return ET014_OK;
}
et014_err_t et014_gfx_bitmap1(et014_gfx_t *g,int x,int y,int w,int h,const uint8_t *data,size_t bytes,size_t stride,uint8_t fg,int bg) { return bitmap(g,x,y,w,h,data,bytes,stride,1,fg,bg); }
et014_err_t et014_gfx_bitmap2(et014_gfx_t *g,int x,int y,int w,int h,const uint8_t *data,size_t bytes,size_t stride) { return bitmap(g,x,y,w,h,data,bytes,stride,2,0,0); }
static int clamp(int v,int lo,int hi) { return v<lo ? lo : (v>hi ? hi:v); }
et014_err_t et014_gfx_gray8(et014_gfx_t *g,int x,int y,int w,int h,const uint8_t *data,size_t bytes,size_t stride,unsigned levels,et014_dither_t dither)
{
    if (!valid(g) || !buffer_valid(data,bytes,stride,w,h,8) || (levels!=2 && levels!=4) || dither<ET014_DITHER_NONE || dither>ET014_DITHER_FLOYD_STEINBERG) return ET014_ERR_ARGUMENT;
    static const uint8_t bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    int32_t *allocation=NULL,*current=NULL,*next=NULL;
    if(dither==ET014_DITHER_FLOYD_STEINBERG) {
        if((size_t)w>SIZE_MAX/(2*sizeof(int32_t))-2) return ET014_ERR_MEMORY;
        allocation=(int32_t*)calloc(((size_t)w+2)*2,sizeof(int32_t));
        if(!allocation) return ET014_ERR_MEMORY;
        current=allocation; next=allocation+(size_t)w+2;
    }
    et014_err_t result=ET014_OK;
    for(int row=0;row<h && result==ET014_OK;++row) {
        for(int col=0;col<w;++col) {
            int value=data[(size_t)row*stride+(size_t)col],step=255/((int)levels-1),q;
            if(dither==ET014_DITHER_FLOYD_STEINBERG) {
                size_t index=(size_t)col+1;
                value=clamp(value*16+current[index],0,255*16);
                q=clamp((value+step*8)/(step*16),0,(int)levels-1);
                int error=value-q*step*16;
                current[index+1]+=error*7/16; next[index-1]+=error*3/16; next[index]+=error*5/16; next[index+1]+=error/16;
            } else if(dither==ET014_DITHER_BAYER4) {
                /* Centered threshold preserves both black and white endpoints. */
                int threshold=2*bayer[(row&3)*4+(col&3)]+1;
                q=clamp((value*32+step*(32-threshold))/(step*32),0,(int)levels-1);
            } else q=(value+step/2)/step;
            uint8_t gray=(uint8_t)(levels==2 ? q*3:q);
            result=point_wide(g,(int64_t)x+col,(int64_t)y+row,gray);
            if(result!=ET014_OK) break;
        }
        if(allocation) { int32_t *swap=current;current=next;next=swap;memset(next,0,((size_t)w+2)*sizeof(int32_t)); }
    }
    free(allocation); return result;
}

static uint32_t utf8_next(const char **text)
{
    const unsigned char *p=(const unsigned char*)*text;
    uint32_t code=*p++; unsigned count;
    if(code<128) { *text=(const char*)p; return code; }
    if(code>=0xC2 && code<=0xDF) { code&=31;count=1; }
    else if(code>=0xE0 && code<=0xEF) { code&=15;count=2; }
    else if(code>=0xF0 && code<=0xF4) { code&=7;count=3; }
    else { ++*text;return 0xFFFD; }
    unsigned length=count+1;
    for(unsigned i=0;i<count;++i) { if(!*p || (*p&0xC0)!=0x80) { ++*text;return 0xFFFD; } code=(code<<6)|(*p++&63); }
    if((length==3 && code<0x800) || (length==4 && code<0x10000) || (code>=0xD800 && code<=0xDFFF) || code>0x10FFFF) { ++*text;return 0xFFFD; }
    *text=(const char*)p;return code;
}
static bool glyph_valid(const et014_glyph_t *glyph)
{
    return glyph->width>=0 && glyph->width<=32767 && glyph->height>=0 && glyph->height<=32767 && glyph->advance>=0 && glyph->advance<=32767 &&
        ((!glyph->width || !glyph->height) || buffer_valid(glyph->bitmap,glyph->data_bytes,glyph->stride,glyph->width,glyph->height,1));
}
static et014_err_t text_run(et014_gfx_t *g,int x,int y,const char *text,const et014_font_t *font,uint8_t fg,int bg,unsigned scale,int *out_width,int *out_height)
{
    if(!text || !scale || scale>32 || fg>3 || bg < -1 || bg>3 || (font && (!font->lookup || font->line_height<1 || font->line_height>32767))) return ET014_ERR_ARGUMENT;
    if(g && !valid(g)) return ET014_ERR_ARGUMENT;
    int64_t advance=0,max_advance=0,line=0;
    int line_height=font ? font->line_height:8;
    bool any=*text!=0;
    while(*text) {
        uint32_t code=utf8_next(&text);
        if(code=='\r') continue;
        if(code=='\n') { if(advance>max_advance) max_advance=advance;advance=0;line+=(int64_t)line_height*scale; if(line>INT_MAX) return ET014_ERR_ARGUMENT;continue; }
        et014_glyph_t glyph={0};
        bool custom=font && font->lookup(font->user,code,&glyph);
        if(custom && !glyph_valid(&glyph)) return ET014_ERR_ARGUMENT;
        int width=custom ? glyph.width:5,height=custom ? glyph.height:7;
        int cell=custom ? glyph.advance:6;
        const uint8_t *rows=ascii_rows[(code>=32 && code<=126 ? code:'?')-32];
        int64_t left=(int64_t)x+advance,top=(int64_t)y+line;
        if(g && bg>=0) TRY(fill_wide(g,left,top,(int64_t)cell*scale,(int64_t)line_height*scale,(uint8_t)bg));
        if(custom) {left+=(int64_t)glyph.x_offset*scale;top+=(int64_t)glyph.y_offset*scale;}
        if(g) {
            /* Clip source iteration too; distant huge glyphs need no pixel loop. */
            et014_rect_t r=intersection(left,top,(int64_t)width*scale,(int64_t)height*scale,g->clip);
            if(r.width) {
                int first_x=(int)(((int64_t)r.x-left)/scale),first_y=(int)(((int64_t)r.y-top)/scale);
                int last_x=(int)(((int64_t)r.x+r.width-1-left)/scale),last_y=(int)(((int64_t)r.y+r.height-1-top)/scale);
                for(int iy=first_y;iy<=last_y;++iy) for(int ix=first_x;ix<=last_x;++ix) {
                    bool set=custom ? ((glyph.bitmap[(size_t)iy*glyph.stride+(size_t)ix/8]>>(7-(ix&7)))&1)!=0 : ((rows[iy]>>(4-ix))&1)!=0;
                    if(set) TRY(fill_wide(g,left+(int64_t)ix*scale,top+(int64_t)iy*scale,scale,scale,fg));
                }
            }
        }
        advance+=(int64_t)cell*scale;
        if(advance>INT_MAX) return ET014_ERR_ARGUMENT;
    }
    if(advance>max_advance)max_advance=advance;
    int64_t total_height=any ? line+(int64_t)line_height*scale:0;
    if(total_height>INT_MAX)return ET014_ERR_ARGUMENT;
    if(out_width)*out_width=(int)max_advance;
    if(out_height)*out_height=(int)total_height;
    return ET014_OK;
}
et014_err_t et014_gfx_text_font(et014_gfx_t *g,int x,int y,const char *text,const et014_font_t *font,uint8_t fg,int bg,unsigned scale)
{
    if(!valid(g))return ET014_ERR_ARGUMENT;
    return text_run(g,x,y,text,font,fg,bg,scale,NULL,NULL);
}
et014_err_t et014_gfx_text(et014_gfx_t *g,int x,int y,const char *text,uint8_t fg,int bg,unsigned scale) { return et014_gfx_text_font(g,x,y,text,NULL,fg,bg,scale); }
et014_err_t et014_gfx_text_size_font(const char *text,const et014_font_t *font,unsigned scale,int *w,int *h)
{
    if(!w || !h)return ET014_ERR_ARGUMENT;
    return text_run(NULL,0,0,text,font,0,-1,scale,w,h);
}
et014_err_t et014_gfx_text_size(const char *text,unsigned scale,int *w,int *h) { return et014_gfx_text_size_font(text,NULL,scale,w,h); }

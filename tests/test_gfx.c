#include "et014_gfx.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while(0)
static et014_err_t begin(void *p,bool profile) { (void)p;(void)profile;return ET014_OK; }
static et014_err_t end(void *p) { (void)p;return ET014_OK; }
static et014_err_t transfer(void *p,const uint8_t *tx,uint8_t *rx,size_t n,uint32_t hz) { (void)p;(void)tx;(void)rx;(void)n;(void)hz;return ET014_OK; }
static int busy(void *p) { (void)p;return 0; }
static int64_t now(void *p) { (void)p;return 0; }
static void delay(void *p,uint32_t n) { (void)p;(void)n; }
static void clear(et014_gfx_t *g)
{
    et014_gfx_reset_clip(g);CHECK(et014_gfx_rotation(g,ET014_ROTATE_0)==ET014_OK);
    CHECK(et014_fill(g->device,ET014_WHITE)==ET014_OK);
}
static int count_color(et014_t *dev,uint8_t color)
{
    int n=0;for(int y=0;y<ET014_HEIGHT;++y)for(int x=0;x<ET014_WIDTH;++x)if(et014_get_pixel(dev,x,y)==color)++n;return n;
}
static void rotations(et014_gfx_t *g)
{
    const int first[4][2]={{0,0},{127,0},{127,295},{0,295}};
    const int last[4][2]={{127,295},{0,295},{0,0},{127,0}};
    for(int rotation=0;rotation<4;++rotation) {
        clear(g);CHECK(et014_gfx_rotation(g,(et014_rotation_t)rotation)==ET014_OK);
        int w=et014_gfx_width(g),h=et014_gfx_height(g);
        CHECK(w==((rotation&1)?296:128));CHECK(h==((rotation&1)?128:296));
        CHECK(et014_gfx_pixel(g,0,0,ET014_BLACK)==ET014_OK);
        CHECK(et014_gfx_pixel(g,w-1,h-1,ET014_DARK_GRAY)==ET014_OK);
        CHECK(et014_get_pixel(g->device,first[rotation][0],first[rotation][1])==ET014_BLACK);
        CHECK(et014_get_pixel(g->device,last[rotation][0],last[rotation][1])==ET014_DARK_GRAY);
        CHECK(et014_gfx_get_pixel(g,w-1,h-1)==ET014_DARK_GRAY);
        CHECK(et014_gfx_pixel(g,-1,0,0)==ET014_OK);CHECK(et014_gfx_pixel(g,w,h,0)==ET014_OK);
        CHECK(count_color(g->device,0)==1);
    }
    CHECK(et014_gfx_rotation(g,(et014_rotation_t)4)==ET014_ERR_ARGUMENT);
}
static void geometry(et014_gfx_t *g)
{
    clear(g);CHECK(et014_gfx_clip(g,10,20,4,3)==ET014_OK);
    CHECK(et014_gfx_clear(g,0)==ET014_OK);CHECK(count_color(g->device,0)==12);
    CHECK(et014_get_pixel(g->device,10,20)==0);CHECK(et014_get_pixel(g->device,9,20)==3);
    CHECK(et014_gfx_invert(g,10,20,4,3)==ET014_OK);CHECK(count_color(g->device,0)==0);
    clear(g);CHECK(et014_gfx_rect(g,5,5,4,3,0,false)==ET014_OK);CHECK(count_color(g->device,0)==10);
    CHECK(et014_get_pixel(g->device,6,6)==3);
    clear(g);CHECK(et014_gfx_rect(g,-2,-2,4,4,1,true)==ET014_OK);CHECK(count_color(g->device,1)==4);
    CHECK(et014_gfx_rect(g,INT_MAX,INT_MAX,INT_MAX,INT_MAX,0,false)==ET014_OK);
    CHECK(et014_gfx_rect(g,0,0,-1,3,0,false)==ET014_ERR_ARGUMENT);
    CHECK(et014_gfx_clip(g,INT_MAX,INT_MIN,INT_MAX,INT_MAX)==ET014_OK);CHECK(g->clip.width==0);
    clear(g);CHECK(et014_gfx_line(g,INT_MIN,4,INT_MAX,4,0)==ET014_OK);CHECK(count_color(g->device,0)==128);
    clear(g);CHECK(et014_gfx_line(g,2,-100,2,INT_MAX,0)==ET014_OK);CHECK(count_color(g->device,0)==296);
    clear(g);CHECK(et014_gfx_line(g,0,0,10,10,0)==ET014_OK);CHECK(count_color(g->device,0)==11);
    for(int i=0;i<=10;++i)CHECK(et014_get_pixel(g->device,i,i)==0);
    clear(g);CHECK(et014_gfx_circle(g,10,10,3,0,false)==ET014_OK);
    CHECK(et014_get_pixel(g->device,10,7)==0);CHECK(et014_get_pixel(g->device,13,10)==0);CHECK(et014_get_pixel(g->device,10,10)==3);
    CHECK(et014_gfx_circle(g,10,10,3,1,true)==ET014_OK);CHECK(et014_get_pixel(g->device,10,10)==1);
    CHECK(et014_gfx_circle(g,INT_MAX,INT_MIN,32767,0,true)==ET014_OK);
    CHECK(et014_gfx_circle(g,0,0,32768,0,true)==ET014_ERR_ARGUMENT);
    clear(g);CHECK(et014_gfx_progress(g,0,0,12,5,50,0,3)==ET014_OK);
    CHECK(et014_get_pixel(g->device,5,2)==0);CHECK(et014_get_pixel(g->device,6,2)==3);
    CHECK(et014_gfx_progress(g,0,0,12,5,101,0,3)==ET014_ERR_ARGUMENT);
}
static void bitmaps(et014_gfx_t *g)
{
    const uint8_t mono[]={0xA8,0xFF,0x50,0xFF};
    clear(g);CHECK(et014_gfx_bitmap1(g,0,0,5,2,mono,sizeof mono,2,0,2)==ET014_OK);
    for(int x=0;x<5;++x)CHECK(et014_gfx_get_pixel(g,x,0)==((x&1)?2:0));
    for(int x=0;x<5;++x)CHECK(et014_gfx_get_pixel(g,x,1)==((x&1)?0:2));
    CHECK(et014_gfx_get_pixel(g,5,0)==3);
    CHECK(et014_gfx_bitmap1(g,0,0,5,2,mono,2,2,0,2)==ET014_ERR_ARGUMENT);
    CHECK(et014_gfx_bitmap1(g,0,0,9,2,mono,4,1,0,2)==ET014_ERR_ARGUMENT);
    clear(g);CHECK(et014_gfx_bitmap1(g,-2,0,5,1,mono,1,1,1,-1)==ET014_OK);
    CHECK(et014_gfx_get_pixel(g,0,0)==1);CHECK(et014_gfx_get_pixel(g,1,0)==3);CHECK(et014_gfx_get_pixel(g,2,0)==1);
    const uint8_t gray[]={0x1B,0x40,0xEE,0xE4,0x80};
    clear(g);CHECK(et014_gfx_bitmap2(g,0,0,5,2,gray,sizeof gray,3)==ET014_OK);
    for(int x=0;x<4;++x){CHECK(et014_gfx_get_pixel(g,x,0)==x);CHECK(et014_gfx_get_pixel(g,x,1)==3-x);}
    CHECK(et014_gfx_get_pixel(g,4,0)==1);CHECK(et014_gfx_get_pixel(g,4,1)==2);
    CHECK(et014_gfx_bitmap2(g,0,0,INT_MAX,INT_MAX,gray,sizeof gray,SIZE_MAX)==ET014_ERR_ARGUMENT);
    clear(g);CHECK(et014_gfx_rotation(g,ET014_ROTATE_90)==ET014_OK);
    CHECK(et014_gfx_bitmap2(g,0,0,5,2,gray,sizeof gray,3)==ET014_OK);
    CHECK(et014_get_pixel(g->device,127,0)==0);CHECK(et014_get_pixel(g->device,126,0)==3);
    CHECK(et014_get_pixel(g->device,127,4)==1);CHECK(et014_get_pixel(g->device,126,4)==2);
}
static void dithering(et014_gfx_t *g)
{
    const uint8_t ramp[]={0,42,43,127,128,212,213,255};
    const uint8_t expected[]={0,0,1,1,2,2,3,3};
    clear(g);CHECK(et014_gfx_gray8(g,0,0,8,1,ramp,sizeof ramp,8,4,ET014_DITHER_NONE)==ET014_OK);
    for(int x=0;x<8;++x)CHECK(et014_gfx_get_pixel(g,x,0)==expected[x]);
    CHECK(et014_gfx_gray8(g,0,1,8,1,ramp,sizeof ramp,8,2,ET014_DITHER_NONE)==ET014_OK);
    for(int x=0;x<8;++x)CHECK(et014_gfx_get_pixel(g,x,1)==(x>=4?3:0));
    uint8_t data[64];memset(data,128,sizeof data);
    clear(g);CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,2,ET014_DITHER_BAYER4)==ET014_OK);
    CHECK(count_color(g->device,0)==32);
    uint8_t snapshot[ET014_FRAMEBUFFER_BYTES];memcpy(snapshot,et014_framebuffer(g->device),sizeof snapshot);
    CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,2,ET014_DITHER_BAYER4)==ET014_OK);
    CHECK(memcmp(snapshot,et014_framebuffer(g->device),sizeof snapshot)==0);
    for(int method=0;method<3;++method) {
        clear(g);memset(data,0,sizeof data);CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,4,(et014_dither_t)method)==ET014_OK);CHECK(count_color(g->device,0)==64);
        memset(data,255,sizeof data);CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,4,(et014_dither_t)method)==ET014_OK);CHECK(count_color(g->device,0)==0);
    }
    memset(data,128,sizeof data);clear(g);CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,2,ET014_DITHER_FLOYD_STEINBERG)==ET014_OK);CHECK(count_color(g->device,0)==32);
    memcpy(snapshot,et014_framebuffer(g->device),sizeof snapshot);
    CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,2,ET014_DITHER_FLOYD_STEINBERG)==ET014_OK);CHECK(memcmp(snapshot,et014_framebuffer(g->device),sizeof snapshot)==0);
    for(int method=0;method<3;++method) {
        for(unsigned i=0;i<sizeof data;++i)data[i]=(uint8_t)(i*4);
        clear(g);CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,4,(et014_dither_t)method)==ET014_OK);
        uint8_t expected_clip[9];for(int y=2;y<5;++y)for(int x=2;x<5;++x)expected_clip[(y-2)*3+x-2]=et014_gfx_get_pixel(g,x,y);
        clear(g);CHECK(et014_gfx_clip(g,2,2,3,3)==ET014_OK);
        CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,4,(et014_dither_t)method)==ET014_OK);
        for(int y=2;y<5;++y)for(int x=2;x<5;++x)CHECK(et014_gfx_get_pixel(g,x,y)==expected_clip[(y-2)*3+x-2]);
        CHECK(et014_gfx_get_pixel(g,1,2)==ET014_WHITE);
    }
    CHECK(et014_gfx_gray8(g,0,0,8,8,data,1,8,4,ET014_DITHER_NONE)==ET014_ERR_ARGUMENT);
    CHECK(et014_gfx_gray8(g,0,0,8,8,data,sizeof data,8,3,ET014_DITHER_NONE)==ET014_ERR_ARGUMENT);
}
static bool glyph(void *p,uint32_t code,et014_glyph_t *out)
{
    (void)p;static const uint8_t bitmap[]={0xF0,0x90,0xF0};
    if(code!=0x4E2D)return false;
    *out=(et014_glyph_t){bitmap,sizeof bitmap,1,4,3,5,1,1};return true;
}
static bool bad_glyph(void *p,uint32_t code,et014_glyph_t *out)
{
    (void)p;(void)code;*out=(et014_glyph_t){NULL,0,0,5,7,6,0,0};return true;
}
static void text(et014_gfx_t *g)
{
    int w,h;CHECK(et014_gfx_text_size("AB\nC",2,&w,&h)==ET014_OK);CHECK(w==24);CHECK(h==32);
    CHECK(et014_gfx_text_size("",1,&w,&h)==ET014_OK);CHECK(w==0 && h==0);
    CHECK(et014_gfx_text_size("\xE4\xB8\xAD",1,&w,&h)==ET014_OK);CHECK(w==6 && h==8);
    CHECK(et014_gfx_text_size("\xF0",1,&w,&h)==ET014_OK);CHECK(w==6);
    CHECK(et014_gfx_text_size("A",0,&w,&h)==ET014_ERR_ARGUMENT);
    clear(g);CHECK(et014_gfx_text(g,0,0,"A",0,3,1)==ET014_OK);
    CHECK(et014_gfx_get_pixel(g,1,0)==0);CHECK(et014_gfx_get_pixel(g,0,0)==3);CHECK(et014_gfx_get_pixel(g,0,3)==0);CHECK(et014_gfx_get_pixel(g,5,3)==3);
    CHECK(et014_gfx_text(g,INT_MAX,INT_MIN,"A\nB",0,-1,32)==ET014_OK);
    et014_font_t font={NULL,glyph,5};
    clear(g);CHECK(et014_gfx_text_font(g,0,0,"\xE4\xB8\xAD",&font,0,-1,2)==ET014_OK);
    CHECK(et014_gfx_get_pixel(g,2,2)==0);CHECK(et014_gfx_get_pixel(g,4,4)==3);CHECK(et014_gfx_get_pixel(g,9,7)==0);
    CHECK(et014_gfx_text_size_font("\xE4\xB8\xAD\n?",&font,2,&w,&h)==ET014_OK);CHECK(w==12 && h==20);
    font.lookup=bad_glyph;CHECK(et014_gfx_text_font(g,0,0,"A",&font,0,-1,1)==ET014_ERR_ARGUMENT);
}
int main(void)
{
    et014_hal_t hal={0};hal.begin=begin;hal.end=end;hal.power_low=end;hal.transfer=transfer;hal.busy=busy;hal.now_us=now;hal.delay_us=delay;hal.delay_ms=delay;
    et014_config_t config;et014_config_default(&config);et014_t *dev=NULL;
    CHECK(et014_create(&hal,&config,&dev)==ET014_OK);et014_gfx_t g;
    CHECK(et014_gfx_init(&g,dev)==ET014_OK);
    rotations(&g);geometry(&g);bitmaps(&g);dithering(&g);text(&g);
    CHECK(et014_gfx_pixel(&g,0,0,4)==ET014_ERR_ARGUMENT);
    CHECK(et014_gfx_init(NULL,dev)==ET014_ERR_ARGUMENT);
    et014_destroy(dev);printf("gfx: %u checks passed\n",checks);return 0;
}

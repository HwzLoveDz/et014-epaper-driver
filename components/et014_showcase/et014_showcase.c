#include "et014_showcase.h"
#include <stdio.h>
#include <string.h>

#define DRAW(call) do { et014_err_t result_=(call);if(result_!=ET014_OK)return result_; } while(0)
static const char *const names[]={"DASHBOARD","DRAWING","TYPE STUDY","MOTION","REST"};
const char *et014_showcase_page_name(et014_showcase_page_t page)
{
    return page>=ET014_SHOWCASE_DASHBOARD && page<ET014_SHOWCASE_PAGE_COUNT ? names[page]:"UNKNOWN";
}
et014_rect_t et014_showcase_motion_rect(void) { et014_rect_t r={20,42,256,66};return r; }
et014_rect_t et014_showcase_motion_native_rect(void) { et014_rect_t r={20,20,66,256};return r; }
static et014_err_t text(et014_gfx_t *g,int x,int y,const char *s,uint8_t color,unsigned scale)
{
    return et014_gfx_text(g,x,y,s,color,-1,scale);
}
static et014_err_t round_fill(et014_gfx_t *g,int x,int y,int w,int h,int radius,uint8_t color)
{
    DRAW(et014_gfx_rect(g,x+radius,y,w-2*radius,h,color,true));
    DRAW(et014_gfx_rect(g,x,y+radius,w,h-2*radius,color,true));
    DRAW(et014_gfx_circle(g,x+radius,y+radius,radius,color,true));
    DRAW(et014_gfx_circle(g,x+w-radius-1,y+radius,radius,color,true));
    DRAW(et014_gfx_circle(g,x+radius,y+h-radius-1,radius,color,true));
    return et014_gfx_circle(g,x+w-radius-1,y+h-radius-1,radius,color,true);
}
static et014_err_t chrome(et014_gfx_t *g,et014_showcase_page_t page,const et014_showcase_metrics_t *m)
{
    DRAW(et014_gfx_rect(g,9,7,7,7,0,true));
    DRAW(et014_gfx_rect(g,12,10,7,7,3,true));
    DRAW(et014_gfx_rect(g,12,10,7,7,0,false));
    DRAW(text(g,26,8,"INK / 014",0,1));
    const char *name=et014_showcase_page_name(page);
    DRAW(text(g,288-(int)strlen(name)*6,8,name,0,1));
    DRAW(et014_gfx_line(g,8,23,287,23,page==ET014_SHOWCASE_MOTION?0:2));
    char step[32];
    if(m->counter)snprintf(step,sizeof step,"STEP %02lu / 12",(unsigned long)m->counter);
    else snprintf(step,sizeof step,"STEP -- / 12");
    DRAW(text(g,288-(int)strlen(step)*6,117,step,0,1));
    return ET014_OK;
}
static et014_err_t dashboard(et014_gfx_t *g,const et014_showcase_metrics_t *m)
{
    DRAW(text(g,12,33,"PANEL SENSOR",0,1));
    char value[16];
    if(m->temperature_valid && m->temperature_c>=-99 && m->temperature_c<=199)snprintf(value,sizeof value,"%d",m->temperature_c);
    else snprintf(value,sizeof value,"--");
    DRAW(text(g,10,52,value,0,6));
    int units_x=10+(int)strlen(value)*36+2;
    DRAW(et014_gfx_circle(g,units_x+2,51,2,0,false));DRAW(text(g,units_x+9,49,"C",0,2));
    DRAW(et014_gfx_line(g,150,34,150,102,2));
    DRAW(text(g,164,33,"LAST REFRESH",0,1));
    char duration[24];
    if(m->last_refresh_ms)snprintf(duration,sizeof duration,"%lu.%02lu s",(unsigned long)(m->last_refresh_ms/1000),(unsigned long)((m->last_refresh_ms%1000)/10));
    else snprintf(duration,sizeof duration,"--");
    unsigned scale=strlen(duration)<=9?2:1;
    DRAW(text(g,164,52,duration,0,scale));
    DRAW(round_fill(g,163,84,124,19,5,0));DRAW(text(g,172,90,"LIVE FROM PANEL",3,1));
    return text(g,10,117,"SIMPLE. LEGIBLE.",0,1);
}
static et014_err_t gallery(et014_gfx_t *g)
{
    /* Original landscape: generated geometry, not a sensor history/forecast. */
    DRAW(round_fill(g,8,31,180,75,6,2));
    DRAW(et014_gfx_circle(g,145,49,11,3,true));
    for(int x=11;x<185;++x) {
        int mountain1=84-(x<74 ? (x-11)*40/63:(140-x)*40/66);
        int mountain2=99-(x<130 ? (x-66)*42/64:(185-x)*42/55);
        if(mountain1<40)mountain1=40;
        if(mountain1>101)mountain1=101;
        if(mountain2<55)mountain2=55;
        if(mountain2>101)mountain2=101;
        DRAW(et014_gfx_line(g,x,mountain1,x,103,1));
        DRAW(et014_gfx_line(g,x,mountain2,x,103,0));
    }
    DRAW(et014_gfx_line(g,8,105,188,105,3));
    DRAW(text(g,207,34,"LIGHT",0,1));DRAW(text(g,207,44,"& SHAPE",0,1));
    for(int i=0;i<4;++i)DRAW(et014_gfx_rect(g,206+i*20,61,15,15,(uint8_t)i,true));
    DRAW(et014_gfx_rect(g,205,60,77,17,0,false));
    uint8_t gradient[80*17];
    for(unsigned y=0;y<17;++y)for(unsigned x=0;x<80;++x)gradient[y*80+x]=(uint8_t)(x*255/79);
    DRAW(et014_gfx_gray8(g,205,86,80,17,gradient,sizeof gradient,80,4,ET014_DITHER_BAYER4));
    return text(g,10,117,"FOUR TONES. ONE CANVAS.",0,1);
}
static et014_err_t typography(et014_gfx_t *g,const et014_font_t *font)
{
    DRAW(text(g,10,36,"Hello,",0,4));
    DRAW(text(g,10,74,"ink.",0,4));
    DRAW(et014_gfx_line(g,172,34,172,104,2));
    DRAW(text(g,187,37,"Aa",0,4));
    DRAW(text(g,188,79,"012345",0,2));
    if(font) {
        DRAW(et014_gfx_clip(g,185,97,103,17));
        et014_err_t result=et014_gfx_text_font(g,188,97,"\xE5\xA2\xA8\xE6\xB0\xB4\xE5\xB1\x8F",font,0,-1,1);
        et014_gfx_reset_clip(g);if(result!=ET014_OK)return result;
    }
    return text(g,10,117,"PIXELS WITH PERSONALITY.",0,1);
}
static et014_err_t motion_contents(et014_gfx_t *g,const et014_showcase_metrics_t *m)
{
    unsigned index=m->animation_index>5?5:m->animation_index;
    uint8_t bg=m->inverted?0:3,fg=m->inverted?3:0;
    et014_rect_t r=et014_showcase_motion_rect();
    DRAW(et014_gfx_rect(g,r.x,r.y,r.width,r.height,bg,true));
    DRAW(et014_gfx_line(g,32,63,262,63,fg));
    for(int i=0;i<5;++i)DRAW(et014_gfx_circle(g,32+i*230/4,63,2,fg,true));
    /* Index 0 is the GC4 background only. All five visible ball positions are
     * established by A2, so the first A2 erasure does not inherit a GC4 ball.
     * This is a mode-history comparison, not an optical ghosting guarantee. */
    if(index) {
        int ball_x=32+((int)index-1)*230/4;
        DRAW(et014_gfx_circle(g,ball_x,63,10,bg,true));
        DRAW(et014_gfx_circle(g,ball_x,63,9,fg,true));
        DRAW(et014_gfx_circle(g,ball_x-2,60,2,bg,true));
    }
    DRAW(et014_gfx_progress(g,24,87,248,10,index*20,fg,bg));
    return ET014_OK;
}
et014_err_t et014_showcase_motion_step(et014_gfx_t *g,const et014_showcase_metrics_t *m)
{
    if(!g || !g->device || !m || g->rotation!=ET014_ROTATE_90)return ET014_ERR_ARGUMENT;
    et014_rect_t r=et014_showcase_motion_rect();
    DRAW(et014_gfx_clip(g,r.x,r.y,r.width,r.height));
    et014_err_t result=motion_contents(g,m);et014_gfx_reset_clip(g);return result;
}
static et014_err_t motion(et014_gfx_t *g,const et014_showcase_metrics_t *m)
{
    DRAW(text(g,20,32,"A SMALL MOVE. A CLEAR CHANGE.",0,1));
    DRAW(et014_showcase_motion_step(g,m));
    return text(g,10,117,"BINARY / FIXED REGION",0,1);
}
static et014_err_t rest(et014_gfx_t *g)
{
    DRAW(et014_gfx_circle(g,42,64,23,0,true));
    DRAW(et014_gfx_circle(g,53,54,21,3,true));
    DRAW(et014_gfx_line(g,78,38,78,98,2));
    DRAW(text(g,96,41,"STILL HERE.",0,2));
    DRAW(text(g,97,70,"The image stays.",0,1));
    DRAW(text(g,97,86,"No refresh is running.",0,1));
    return text(g,10,117,"END / STATIC IMAGE",0,1);
}
et014_err_t et014_showcase_draw(et014_gfx_t *g,et014_showcase_page_t page,const et014_showcase_metrics_t *m,const et014_font_t *font)
{
    if(!g || !g->device || !m || page<ET014_SHOWCASE_DASHBOARD || page>=ET014_SHOWCASE_PAGE_COUNT)return ET014_ERR_ARGUMENT;
    DRAW(et014_gfx_rotation(g,ET014_ROTATE_90));DRAW(et014_gfx_clear(g,3));
    DRAW(chrome(g,page,m));
    et014_err_t result;
    switch(page) {
    case ET014_SHOWCASE_DASHBOARD:result=dashboard(g,m);break;
    case ET014_SHOWCASE_GALLERY:result=gallery(g);break;
    case ET014_SHOWCASE_TYPOGRAPHY:result=typography(g,font);break;
    case ET014_SHOWCASE_MOTION:result=motion(g,m);break;
    case ET014_SHOWCASE_REST:result=rest(g);break;
    default:return ET014_ERR_ARGUMENT;
    }
    if(result!=ET014_OK)return result;
    /* motion_step already applied its area theme; global inversion would undo
     * it. Outside that area invert separately to maintain consistent themes. */
    if(m->inverted && page==ET014_SHOWCASE_MOTION) {
        et014_rect_t r=et014_showcase_motion_rect();
        DRAW(et014_gfx_invert(g,0,0,296,r.y));
        DRAW(et014_gfx_invert(g,0,r.y+r.height,296,128-r.y-r.height));
        DRAW(et014_gfx_invert(g,0,r.y,r.x,r.height));
        return et014_gfx_invert(g,r.x+r.width,r.y,296-r.x-r.width,r.height);
    }
    if(m->inverted)return et014_gfx_invert(g,0,0,296,128);
    return ET014_OK;
}

/* Host-only renderer of the actual production graphics code; no panel access.
 * Usage: render_gfx existing-output-directory
 * Four native 128x296 P5 PGM files exercise every logical rotation.
 */
#include "et014_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static et014_err_t begin(void *p,bool profile) { (void)p;(void)profile;return ET014_OK; }
static et014_err_t end(void *p) { (void)p;return ET014_OK; }
static et014_err_t transfer(void *p,const uint8_t *tx,uint8_t *rx,size_t n,uint32_t hz) { (void)p;(void)tx;(void)rx;(void)n;(void)hz;return ET014_OK; }
static int busy(void *p) { (void)p;return 0; }
static int64_t now(void *p) { (void)p;return 0; }
static void delay(void *p,uint32_t n) { (void)p;(void)n; }
#define DRAW(call) do { et014_err_t e=(call); if(e!=ET014_OK) { fprintf(stderr,"render error %s: %s\n",#call,et014_error_string(e));return false;} }while(0)

static bool scene(et014_gfx_t *g,et014_rotation_t rotation)
{
    DRAW(et014_gfx_rotation(g,rotation));DRAW(et014_gfx_clear(g,3));
    int w=et014_gfx_width(g),h=et014_gfx_height(g);
    char title[40];snprintf(title,sizeof(title),"ET014 %dx%d ROT%d",w,h,(int)rotation*90);
    DRAW(et014_gfx_rect(g,0,0,w,h,0,false));
    DRAW(et014_gfx_text(g,4,4,title,0,-1,1));
    for(int level=0;level<4;++level) {
        int left=4+level*(w-8)/4,width=(w-8)/4;
        DRAW(et014_gfx_rect(g,left,18,width,24,(uint8_t)level,true));
        char label[2]={(char)('0'+level),0};
        DRAW(et014_gfx_text(g,left+width/2-2,46,label,0,-1,1));
    }
    DRAW(et014_gfx_text(g,4,61,"ASCII abc 0123 !?",0,-1,1));
    DRAW(et014_gfx_progress(g,4,76,w-8,10,67,0,3));
    uint8_t gradient[296*16];int gw=w-8;
    for(int y=0;y<16;++y)for(int x=0;x<gw;++x)gradient[y*gw+x]=(uint8_t)(x*255/(gw-1));
    DRAW(et014_gfx_gray8(g,4,94,gw,16,gradient,(size_t)gw*16,(size_t)gw,4,ET014_DITHER_BAYER4));
    if(h>128) {
        DRAW(et014_gfx_text(g,4,119,"BAYER 4 GRAY",0,-1,1));
        DRAW(et014_gfx_gray8(g,4,135,gw,16,gradient,(size_t)gw*16,(size_t)gw,2,ET014_DITHER_FLOYD_STEINBERG));
        DRAW(et014_gfx_text(g,4,157,"FS BINARY",0,-1,1));
        DRAW(et014_gfx_circle(g,32,199,20,0,false));
        DRAW(et014_gfx_circle(g,92,199,20,1,true));
        DRAW(et014_gfx_line(g,6,175,56,224,0));
        DRAW(et014_gfx_line(g,6,224,56,175,0));
        DRAW(et014_gfx_text(g,4,242,"PIXEL",0,-1,2));
        DRAW(et014_gfx_clip(g,4,270,w-8,15));
        for(int x=0;x<w;++x)DRAW(et014_gfx_line(g,x,266,x,290,(uint8_t)(x&1 ? 3:0)));
        et014_gfx_reset_clip(g);
    }
    return true;
}
static bool save_pgm(const char *path,et014_t *device)
{
    FILE *file=fopen(path,"wb");if(!file){perror(path);return false;}
    bool success=fprintf(file,"P5\n128 296\n255\n")>0;
    for(int y=0;y<ET014_HEIGHT && success;++y) {
        uint8_t row[ET014_WIDTH];for(int x=0;x<ET014_WIDTH;++x)row[x]=(uint8_t)(85*et014_get_pixel(device,x,y));
        success=fwrite(row,1,sizeof(row),file)==sizeof(row);
    }
    if(fclose(file)!=0)success=false;
    return success;
}
int main(int argc,char **argv)
{
    if(argc!=2){fprintf(stderr,"Usage: render_gfx existing-output-directory\n");return 2;}
    et014_hal_t hal={0};hal.begin=begin;hal.end=end;hal.power_low=end;hal.transfer=transfer;hal.busy=busy;hal.now_us=now;hal.delay_us=delay;hal.delay_ms=delay;
    et014_config_t config;et014_config_default(&config);et014_t *device=NULL;et014_gfx_t g;
    if(et014_create(&hal,&config,&device)!=ET014_OK)return 1;
    if(et014_gfx_init(&g,device)!=ET014_OK){et014_destroy(device);return 1;}
    int result=0;
    for(int rotation=0;rotation<4;++rotation) {
        char path[1024];int length=snprintf(path,sizeof path,"%s/gfx-rotation-%d.pgm",argv[1],rotation*90);
        if(length<0 || (size_t)length>=sizeof path || !scene(&g,(et014_rotation_t)rotation) || !save_pgm(path,device)){result=1;break;}
        printf("Rendered %s: 128x296 native pixels; production graphics, host only\n",path);
    }
    et014_destroy(device);return result;
}

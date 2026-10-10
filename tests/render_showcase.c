/* Visual fixture from actual showcase production code. No hardware I/O. */
#include "et014_showcase.h"
#include <stdio.h>
#include <stdlib.h>

static et014_err_t begin(void *p,bool profile) { (void)p;(void)profile;return ET014_OK; }
static et014_err_t end(void *p) { (void)p;return ET014_OK; }
static et014_err_t transfer(void *p,const uint8_t *tx,uint8_t *rx,size_t n,uint32_t hz) { (void)p;(void)tx;(void)rx;(void)n;(void)hz;return ET014_OK; }
static int busy(void *p) { (void)p;return 0; }
static int64_t now(void *p) { (void)p;return 0; }
static void delay(void *p,uint32_t n) { (void)p;(void)n; }
static bool save(const char *dir,const char *name,et014_gfx_t *g)
{
    for(unsigned native=0;native<2;++native) {
        char path[1024];int length=snprintf(path,sizeof path,"%s/%s-%s.pgm",dir,name,native?"native":"landscape");
        if(length<0 || (size_t)length>=sizeof path)return false;
        FILE *f=fopen(path,"wb");if(!f){perror(path);return false;}
        int w=native?128:296,h=native?296:128;
        bool ok=fprintf(f,"P5\n%d %d\n255\n",w,h)>0;
        for(int y=0;y<h && ok;++y) {
            uint8_t row[296];
            for(int x=0;x<w;++x)row[x]=(uint8_t)(85*(native?et014_get_pixel(g->device,x,y):et014_gfx_get_pixel(g,x,y)));
            ok=fwrite(row,1,(size_t)w,f)==(size_t)w;
        }
        if(fclose(f)!=0)ok=false;
        if(!ok)return false;
        printf("%s\n",path);
    }
    return true;
}
int main(int argc,char **argv)
{
    if(argc!=2){fprintf(stderr,"Usage: render_showcase existing-output-directory\n");return 2;}
    et014_hal_t hal={0};hal.begin=begin;hal.end=end;hal.power_low=end;hal.transfer=transfer;hal.busy=busy;hal.now_us=now;hal.delay_us=delay;hal.delay_ms=delay;
    et014_config_t config;et014_config_default(&config);et014_t *device=NULL;et014_gfx_t g;
    if(et014_create(&hal,&config,&device)!=ET014_OK)return 1;
    if(et014_gfx_init(&g,device)!=ET014_OK){et014_destroy(device);return 1;}
    /* Deterministic fixture values are not represented as measurements. */
    et014_showcase_metrics_t m={.temperature_c=29,.temperature_valid=true,.last_refresh_ms=1194,.counter=1,.animation_index=0,.inverted=false};
    const char *const names[]={"dashboard","gallery","typography","motion-0","rest"};
    const uint32_t steps[]={1,2,4,5,12};
    int result=0;
    for(int page=0;page<ET014_SHOWCASE_PAGE_COUNT;++page) {
        m.counter=steps[page];
        if(et014_showcase_draw(&g,(et014_showcase_page_t)page,&m,NULL)!=ET014_OK || !save(argv[1],names[page],&g)){result=1;break;}
        if(page==ET014_SHOWCASE_GALLERY) {
            m.inverted=true;m.counter=3;
            if(et014_showcase_draw(&g,ET014_SHOWCASE_GALLERY,&m,NULL)!=ET014_OK || !save(argv[1],"gallery-inverted",&g)){result=1;break;}
            m.inverted=false;m.counter=steps[page];
        }
        if(page==ET014_SHOWCASE_MOTION) {
            for(unsigned index=1;index<=5;++index) {
                m.animation_index=index;char name[24];snprintf(name,sizeof name,"motion-%u",index);
                if(et014_showcase_motion_step(&g,&m)!=ET014_OK || !save(argv[1],name,&g)){result=1;break;}
            }
            m.animation_index=0;if(result)break;
        }
    }
    et014_destroy(device);return result;
}

/* Render the production showcase without hardware, and audit the actual plan. */
#include "et014_showcase.h"
#include "et014_showcase_plan.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { ++checks; if (!(test)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while(0)
static unsigned checks, hal_calls;
static uint8_t waveform[74472];
static unsigned le16(size_t p)
{CHECK(p+2<=sizeof(waveform));return waveform[p]|((unsigned)waveform[p+1]<<8);}
static size_t le32(size_t p)
{return le16(p)|((size_t)le16(p+2)<<16);}
static size_t bucket(int temperature)
{
    unsigned best=0,distance=999;
    for(unsigned i=0;i<10;++i){
        unsigned d=(unsigned)abs(temperature-(int)le16(48+20*i));
        if(d<distance){best=i;distance=d;}
    }
    return 48+20*best;
}
static unsigned frame_count(size_t directory,unsigned mode)
{return le16(le32(directory+4+4*mode));}
static et014_err_t begin(void *user,bool profile) { (void)user;(void)profile;++hal_calls;return ET014_OK; }
static et014_err_t end(void *user) { (void)user;++hal_calls;return ET014_OK; }
static et014_err_t transfer(void *user,const uint8_t *tx,uint8_t *rx,size_t n,uint32_t hz)
{ (void)user;(void)tx;(void)rx;(void)n;(void)hz;++hal_calls;return ET014_OK; }
static int busy(void *user) { (void)user;++hal_calls;return 0; }
static int64_t now(void *user) { (void)user;++hal_calls;return 0; }
static void delay(void *user,uint32_t amount) { (void)user;(void)amount;++hal_calls; }
static bool binary(const uint8_t *buffer)
{
    for(unsigned i=0;i<ET014_FRAMEBUFFER_BYTES;++i)
        if(((buffer[i]^(buffer[i]>>1))&0x55)!=0)return false;
    return true;
}
static unsigned pixel(const uint8_t *buffer,int x,int y)
{return (buffer[y*ET014_STRIDE+x/4]>>(6-2*(x%4)))&3;}
static bool contains(et014_rect_t r,int x,int y)
{return x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height;}
static void histogram(const uint8_t *buffer,unsigned counts[4])
{
    memset(counts,0,4*sizeof(*counts));
    for(int y=0;y<ET014_HEIGHT;++y)for(int x=0;x<ET014_WIDTH;++x)++counts[pixel(buffer,x,y)];
}

static void test_pages(et014_gfx_t *g,et014_t *device)
{
    et014_showcase_metrics_t metrics={.temperature_c=32,.temperature_valid=true,.last_refresh_ms=3550,.counter=7,.animation_index=0,.inverted=false};
    uint8_t before[ET014_FRAMEBUFFER_BYTES];
    for(int page=0;page<ET014_SHOWCASE_PAGE_COUNT;++page){
        CHECK(et014_showcase_page_name((et014_showcase_page_t)page)!=NULL);
        CHECK(strlen(et014_showcase_page_name((et014_showcase_page_t)page))>0);
        CHECK(et014_showcase_draw(g,(et014_showcase_page_t)page,&metrics,NULL)==ET014_OK);
        CHECK(g->rotation==ET014_ROTATE_90);
        CHECK(et014_gfx_width(g)==296 && et014_gfx_height(g)==128);
        unsigned colors[4];histogram(et014_framebuffer(device),colors);
        CHECK(colors[0]>100 && colors[3]>100);
        if(page==ET014_SHOWCASE_GALLERY)CHECK(colors[1]>0 && colors[2]>0);
        if(page==ET014_SHOWCASE_MOTION)CHECK(binary(et014_framebuffer(device)));
        memcpy(before,et014_framebuffer(device),sizeof(before));
        CHECK(et014_showcase_draw(g,(et014_showcase_page_t)page,&metrics,NULL)==ET014_OK);
        CHECK(memcmp(before,et014_framebuffer(device),sizeof(before))==0);
    }
    metrics.temperature_valid=false;
    CHECK(et014_showcase_draw(g,ET014_SHOWCASE_DASHBOARD,&metrics,NULL)==ET014_OK);
    CHECK(hal_calls==0); /* drawing must never wake panel or perform a refresh */
}

static void test_motion_region(et014_gfx_t *g,et014_t *device)
{
    et014_showcase_metrics_t metrics={.temperature_c=32,.temperature_valid=true,.last_refresh_ms=750,.counter=1,.animation_index=0,.inverted=false};
    et014_rect_t logical=et014_showcase_motion_rect(),native=et014_showcase_motion_native_rect();
    CHECK(logical.x>=0 && logical.y>=0 && logical.x+logical.width<=296 && logical.y+logical.height<=128);
    CHECK(native.x==128-logical.y-logical.height && native.y==logical.x);
    CHECK(native.width==logical.height && native.height==logical.width);
    CHECK(native.x>=0 && native.y>=0 && native.x+native.width<=128 && native.y+native.height<=296);
    CHECK(et014_showcase_draw(g,ET014_SHOWCASE_MOTION,&metrics,NULL)==ET014_OK);
    CHECK(binary(et014_framebuffer(device)));
    /* Regression: GC4 must not establish the first moving ball. Check the
     * full old-ball box against the empty background, excluding track/marker. */
    uint8_t empty_track[ET014_FRAMEBUFFER_BYTES];
    memcpy(empty_track,et014_framebuffer(device),sizeof(empty_track));
    for(int y=53;y<=73;++y)for(int x=22;x<=42;++x)
        if(y<61 || y>65)CHECK(et014_gfx_get_pixel(g,x,y)==ET014_WHITE);
    metrics.animation_index=1;
    CHECK(et014_showcase_motion_step(g,&metrics)==ET014_OK);
    CHECK(et014_gfx_get_pixel(g,32,57)==ET014_BLACK);
    metrics.animation_index=2;
    CHECK(et014_showcase_motion_step(g,&metrics)==ET014_OK);
    for(int y=53;y<=73;++y)for(int x=22;x<=42;++x)
        CHECK(et014_gfx_get_pixel(g,x,y)==pixel(empty_track,127-y,x));
    metrics.animation_index=0;
    CHECK(et014_showcase_draw(g,ET014_SHOWCASE_MOTION,&metrics,NULL)==ET014_OK);
    uint8_t previous[ET014_FRAMEBUFFER_BYTES];
    for(unsigned frame=1;frame<=5;++frame){
        memcpy(previous,et014_framebuffer(device),sizeof(previous));
        metrics.animation_index=frame;
        /* Changing telemetry must not redraw header/footer outside update area. */
        metrics.temperature_c=20+(int)frame;metrics.counter=100+frame;metrics.last_refresh_ms=301+frame;
        metrics.inverted=(frame==5);
        CHECK(et014_showcase_motion_step(g,&metrics)==ET014_OK);
        CHECK(binary(et014_framebuffer(device)));
        unsigned differences=0;
        for(int y=0;y<296;++y)for(int x=0;x<128;++x){
            if(pixel(previous,x,y)!=et014_get_pixel(device,x,y)){
                ++differences;CHECK(contains(native,x,y));
            }
        }
        CHECK(differences>0);
        CHECK(g->clip.x==0 && g->clip.y==0 && g->clip.width==296 && g->clip.height==128);
    }
    memcpy(previous,et014_framebuffer(device),sizeof(previous));
    CHECK(et014_showcase_motion_step(g,&metrics)==ET014_OK);
    CHECK(memcmp(previous,et014_framebuffer(device),sizeof(previous))==0);
    metrics.animation_index=1000;
    CHECK(et014_showcase_motion_step(g,&metrics)==ET014_OK);
    CHECK(memcmp(previous,et014_framebuffer(device),sizeof(previous))==0); /* clamps at final frame */
    CHECK(et014_gfx_rotation(g,ET014_ROTATE_0)==ET014_OK);
    memcpy(previous,et014_framebuffer(device),sizeof(previous));
    CHECK(et014_showcase_motion_step(g,&metrics)!=ET014_OK);
    CHECK(memcmp(previous,et014_framebuffer(device),sizeof(previous))==0);
    CHECK(hal_calls==0);
}

static bool same_rect(et014_rect_t a,et014_rect_t b)
{return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;}

static void test_actual_plan(et014_gfx_t *g,et014_t *device)
{
    CHECK(et014_showcase_plan_valid());
    CHECK(et014_showcase_plan_count()==12 && et014_showcase_plan_count()<=ET014_SHOWCASE_MAX_UPDATES);
    CHECK(ET014_SHOWCASE_MAX_UPDATES==12);
    CHECK(ET014_SHOWCASE_MIN_A2_HOLD_MS==3500);
    CHECK(et014_showcase_plan_step(12)==NULL && et014_showcase_plan_step(SIZE_MAX)==NULL);
    CHECK(same_rect(et014_showcase_motion_region(),et014_showcase_motion_native_rect()));
    unsigned clean=0,a2=0,samples=0,hold=0,modes[4]={0};
    uint8_t previous[ET014_FRAMEBUFFER_BYTES];
    et014_showcase_metrics_t metrics={.temperature_c=32,.temperature_valid=true,.last_refresh_ms=1000};
    for(size_t index=0;index<et014_showcase_plan_count();++index){
        const et014_showcase_step_t *step=et014_showcase_plan_step(index);
        CHECK(step!=NULL);CHECK(step->mode>=ET014_GC4 && step->mode<=ET014_A2);
        ++modes[step->mode];hold+=step->hold_after_ms;
        if(step->sample_temp)++samples;
        if(step->force_clean){++clean;CHECK(index==0 || index==11);CHECK(step->mode==ET014_GC4);}
        memcpy(previous,et014_framebuffer(device),sizeof(previous));
        metrics.animation_index=step->animation_index;metrics.inverted=step->inverted;
        if(step->mode==ET014_A2){
            ++a2;CHECK(step->use_region);CHECK(step->animation_index==a2);
            CHECK(step->hold_after_ms>=ET014_SHOWCASE_MIN_A2_HOLD_MS);CHECK(!step->force_clean && !step->sample_temp);
            CHECK(binary(previous));
            CHECK(et014_showcase_motion_step(g,&metrics)==ET014_OK);
            CHECK(binary(et014_framebuffer(device)));
            unsigned changed=0;
            for(int y=0;y<296;++y)for(int x=0;x<128;++x){
                if(pixel(previous,x,y)!=et014_get_pixel(device,x,y)){
                    ++changed;CHECK(contains(step->region,x,y));
                }
            }
            CHECK(changed>0);
        }else{
            metrics.counter=(uint32_t)index+1;
            CHECK(!step->use_region);
            CHECK(et014_showcase_draw(g,step->page,&metrics,NULL)==ET014_OK);
        }
        if(index==11)CHECK(step->page==ET014_SHOWCASE_REST && step->hold_after_ms==0);
    }
    CHECK(clean==2 && a2==5 && samples==3 && hold==58500);
    CHECK(modes[ET014_GC4]==7 && modes[ET014_GU4]==0 && modes[ET014_A2]==5);
    CHECK(hal_calls==0);
}

static void test_budget(const char *fixture)
{
    FILE *input=fopen(fixture,"rb");CHECK(input!=NULL);
    CHECK(fread(waveform,1,sizeof(waveform),input)==sizeof(waveform));
    CHECK(fgetc(input)==EOF);CHECK(fclose(input)==0);
    CHECK(le16(0)==4 && le32(4)==sizeof(waveform) && le16(8)==10 && le16(10)==4);
    unsigned checksum=0;
    for(size_t i=0;i<sizeof(waveform);++i)if(i!=2 && i!=3)checksum+=waveform[i];
    CHECK((unsigned)((-checksum)&0xffff)==le16(2));
    unsigned maximum_frames=0;
    for(int temperature=0;temperature<=40;++temperature){
        size_t directory=bucket(temperature);
        et014_showcase_budget_t actual;
        CHECK(et014_showcase_plan_budget(temperature,&actual)==ET014_OK);
        CHECK(actual.input_temperature_c==temperature);
        CHECK(actual.waveform_temperature_c==(int)le16(directory));
        unsigned init=0,image=0,clean=0,holds=0,max_hold=0,samples=0,modes[4]={0};
        for(size_t index=0;index<et014_showcase_plan_count();++index){
            const et014_showcase_step_t *step=et014_showcase_plan_step(index);
            image+=frame_count(directory,(unsigned)step->mode);
            if(step->force_clean){init+=frame_count(directory,0);++clean;}
            holds+=step->hold_after_ms;if(step->hold_after_ms>max_hold)max_hold=step->hold_after_ms;
            if(step->sample_temp)++samples;
            ++modes[step->mode];
        }
        CHECK(actual.update_count==12 && actual.clean_count==clean);
        CHECK(actual.temperature_samples==samples && samples==3);
        CHECK(actual.init_scan_frames==init && init==242);
        CHECK(actual.image_scan_frames==image && actual.total_scan_frames==init+image);
        CHECK(actual.total_scan_bytes==(uint64_t)(init+image)*30836u);
        CHECK(actual.total_hold_ms==holds && holds==58500 && actual.maximum_hold_ms==max_hold && max_hold==8000);
        CHECK(memcmp(actual.mode_updates,modes,sizeof(modes))==0);
        CHECK(actual.nominal_scan_ms==(init+image)*20u);
        CHECK(actual.frame_limit_scan_ms==(init+image)*23u);
        CHECK(actual.programmed_power_delay_ms==12u*330u);
        CHECK(actual.nominal_plan_ms==holds+(init+image)*20u+12u*330u);
        CHECK(actual.frame_limit_plan_ms==holds+(init+image)*23u+12u*330u);
        if(init+image>maximum_frames)maximum_frames=init+image;
        if(temperature==29 || temperature==32)CHECK(actual.total_scan_frames==534 && actual.nominal_plan_ms==73140);
    }
    et014_showcase_budget_t worst;
    et014_showcase_plan_worst_case(&worst);et014_showcase_plan_worst_case(NULL);
    CHECK(worst.total_scan_frames==maximum_frames && maximum_frames==1304);
    CHECK(worst.nominal_plan_ms==88540 && worst.frame_limit_plan_ms==92452);
    CHECK(et014_showcase_plan_budget(-1,&worst)==ET014_ERR_TEMPERATURE);
    CHECK(et014_showcase_plan_budget(41,&worst)==ET014_ERR_TEMPERATURE);
    CHECK(et014_showcase_plan_budget(29,NULL)==ET014_ERR_ARGUMENT);
    CHECK(et014_showcase_replay_allowed(0,UINT64_MAX,false));
    CHECK(!et014_showcase_replay_allowed(999,1000,true));
    CHECK(!et014_showcase_replay_allowed(1000,1000,true));
    CHECK(!et014_showcase_replay_allowed(60999,1000,true));
    CHECK(et014_showcase_replay_allowed(61000,1000,true));
    CHECK(et014_showcase_replay_allowed(UINT64_MAX,UINT64_MAX-60000,true));
    CHECK(!et014_showcase_replay_allowed(0,UINT64_MAX-60000,true));
}

int main(int argc,char **argv)
{
    CHECK(argc==2);test_budget(argv[1]);
    et014_hal_t hal={0};hal.begin=begin;hal.end=end;hal.power_low=end;hal.transfer=transfer;
    hal.busy=busy;hal.now_us=now;hal.delay_us=delay;hal.delay_ms=delay;
    et014_config_t config;et014_config_default(&config);et014_t *device=NULL;
    CHECK(et014_create(&hal,&config,&device)==ET014_OK);
    et014_gfx_t graphics;CHECK(et014_gfx_init(&graphics,device)==ET014_OK);
    test_pages(&graphics,device);test_motion_region(&graphics,device);test_actual_plan(&graphics,device);
    et014_destroy(device);
    printf("showcase: %u checks passed; rendering and finite-plan tests only, no lifetime claim\n",checks);
    return 0;
}

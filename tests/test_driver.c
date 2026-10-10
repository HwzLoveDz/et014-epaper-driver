/* Host-only tests of the real portable driver, never hardware acceptance.
 * Expected wire layout below is deliberately independent of driver helpers. */
#include "et014.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
static unsigned checks, cases;
static unsigned char *waveform;
static size_t waveform_size;

static unsigned le16(size_t p) { CHECK(p + 2 <= waveform_size); return waveform[p] | ((unsigned)waveform[p+1] << 8); }
static size_t le32(size_t p) { CHECK(p + 4 <= waveform_size); return le16(p) | ((size_t)le16(p+2) << 16); }
static size_t bucket(int temp) {
    unsigned i, best = 0, distance = 999;
    for (i=0; i<10; ++i) {
        int t = (int)le16(48 + i*20), d = abs(temp-t);
        if ((unsigned)d < distance) { best=i; distance=(unsigned)d; }
    }
    return 48 + best*20;
}
static size_t mode_offset(int temp, unsigned mode) { return le32(bucket(temp)+4+mode*4); }
static unsigned frame_count(int temp, unsigned mode) { return le16(mode_offset(temp,mode)); }
static unsigned drive_code(int temp, unsigned mode, unsigned frame, unsigned old, unsigned now) {
    unsigned state = (old << 4) | now;
    size_t off = mode_offset(temp,mode)+4+frame*64+state/4;
    CHECK(off < waveform_size);
    return (waveform[off] >> (2*(state%4))) & 3;
}

typedef struct {
    int64_t clock;
    unsigned calls, begins, ends, power_lows, frames, packets, triggers, packet_in_frame;
    unsigned faults, selected_lines, checked_sources, yield_calls;
    unsigned lifecycle_step, last_end_step, last_low_step;
    int id_bad, status_bad, not_ready, busy_stuck, busy_error, cancel_now, cancel_at_frame;
    int begin_fail, end_fail, power_low_fail, fail_reg, fail_value, fail_packet;
    unsigned extra_us;
    bool profile, active, audit, source_audit;
    int temperature;
    unsigned expected_init, expected_mode;
    unsigned char source_old[ET014_FRAMEBUFFER_BYTES], source_new[ET014_FRAMEBUFFER_BYTES];
    unsigned char retained_source[32];
    unsigned char power_values[32];
    unsigned power_count;
} fake_t;

static unsigned pixel_at(const unsigned char *fb, unsigned x, unsigned y) { return (fb[y*32+x/4] >> ((3-(x%4))*2)) & 3; }
static et014_err_t fake_begin(void *user, bool profile) {
    fake_t *f=user; ++f->begins; f->profile=profile; f->active=true; f->clock+=10000;
    return f->begin_fail ? ET014_ERR_IO : ET014_OK;
}
static et014_err_t fake_end(void *user) { fake_t *f=user; ++f->ends; f->last_end_step=++f->lifecycle_step; f->active=false; return f->end_fail?ET014_ERR_IO:ET014_OK; }
static et014_err_t fake_low(void *user) { fake_t *f=user; ++f->power_lows; f->last_low_step=++f->lifecycle_step; return f->power_low_fail?ET014_ERR_IO:ET014_OK; }
static int fake_busy(void *user) { fake_t *f=user; ++f->clock; return f->busy_error ? -1 : f->busy_stuck; }
static int64_t fake_now(void *user) { fake_t *f=user; return ++f->clock; }
static void fake_delay_us(void *user, uint32_t us) { ((fake_t *)user)->clock+=us; }
static void fake_delay_ms(void *user, uint32_t ms) { ((fake_t *)user)->clock+=(int64_t)ms*1000; }
static bool fake_cancelled(void *user) { fake_t *f=user; return f->cancel_now || (f->cancel_at_frame>0 && f->frames >= (unsigned)f->cancel_at_frame); }
static void fake_yield(void *user) { ++((fake_t *)user)->yield_calls; }

static void audit_packet(fake_t *f, const uint8_t *tx) {
    unsigned i, p=f->packet_in_frame, row=p/2;
    CHECK(tx[0]==0x70 && tx[1]==0x0a && tx[2]==0xc0);
    for(i=3;i<8;++i) CHECK(tx[i]==0);
    if (p<592 && p%2==1) {
        unsigned gate=295-row, select=gate%16, block=gate/16;
        for(i=0;i<4;++i) CHECK(tx[8+i] == (i==select/4 ? (0xc0u >> ((select%4)*2)) : 0));
        for(i=0;i<5;++i) CHECK(tx[44+i] == (i==block/4 ? (0xc0u >> ((block%4)*2)) : 0));
        ++f->selected_lines;
        if (f->source_audit) {
            for(i=0;i<32;++i) {
                unsigned b=0,k;
                for(k=0;k<4;++k) {
                    unsigned code;
                    if(f->frames<f->expected_init) code = f->frames==120 ? 0 : ((f->frames/20)%2 ? 2 : 3);
                    else code=drive_code(f->temperature,f->expected_mode,f->frames-f->expected_init,
                        pixel_at(f->source_old,i*4+k,row),pixel_at(f->source_new,i*4+k,row));
                    b=(b<<2)|code;
                }
                CHECK(tx[12+i]==b); ++f->checked_sources;
            }
        }
        memcpy(f->retained_source,tx+12,32);
    } else {
        for(i=0;i<4;++i) CHECK(tx[8+i]==0);
        for(i=0;i<5;++i) CHECK(tx[44+i]==0);
        if(p<592) CHECK(memcmp(tx+12,f->retained_source,32)==0);
        else for(i=12;i<44;++i) CHECK(tx[i]==0);
    }
    ++f->packet_in_frame;
    if(f->packet_in_frame==593) { f->packet_in_frame=0; ++f->frames; }
}
static et014_err_t fake_transfer(void *user, const uint8_t *tx, uint8_t *rx, size_t n, uint32_t hz) {
    fake_t *f=user;
    CHECK(f->active); CHECK(tx!=NULL && n>0 && n<=49); CHECK(hz>0 && hz<=20000000);
    ++f->calls; f->clock+=(int64_t)((n*8000000ull+hz-1)/hz)+1+f->extra_us;
    if(rx) memset(rx,0,n);
    if(n==3 && tx[0]==0x71) { if(rx) rx[1]=rx[2]=(uint8_t)(f->id_bad?0:0x12); }
    if(n==5 && tx[0]==0x73 && rx) {
        unsigned char code=(unsigned char)(f->status_bad?0xff:(f->not_ready?0x10:0x50));
        for(size_t j=1;j<n;++j) rx[j]=code;
    }
    if(n==3 && tx[0]==0x70 && tx[1]==5) {
        CHECK(f->power_count<sizeof(f->power_values)); f->power_values[f->power_count++]=tx[2];
    }
    if(!f->faults && ((n==3 && tx[0]==0x70 && f->fail_reg>=0 && tx[1]==(unsigned)f->fail_reg && tx[2]==(unsigned)f->fail_value)
       || (n==49 && f->fail_packet>0 && f->packets+1==(unsigned)f->fail_packet))) {
        ++f->faults; return ET014_ERR_IO;
    }
    if(n==49) {
        ++f->packets;
        if(f->audit && fake_cancelled(f)) {
            CHECK(tx[0]==0x70 && tx[1]==0x0a && tx[2]==0xc0);
            for(size_t j=3;j<49;++j) CHECK(tx[j]==0); /* abort ResetOutput */
        } else if(f->audit) audit_packet(f,tx);
    }
    if(n==3 && tx[0]==0x70 && tx[1]==2 && tx[2]==15) ++f->triggers;
    return ET014_OK;
}
static et014_hal_t hal_for(fake_t *f) {
    et014_hal_t h={f,fake_begin,fake_end,fake_low,fake_transfer,fake_busy,fake_now,fake_delay_us,fake_delay_ms,fake_cancelled,fake_yield}; return h;
}
static void fake_init(fake_t *f) { memset(f,0,sizeof(*f)); f->fail_reg=-1; f->temperature=29; }
static et014_t *new_device(fake_t *f) {
    et014_t *d=NULL; et014_hal_t hal; et014_config_t cfg;
    fake_init(f); hal=hal_for(f); et014_config_default(&cfg); cfg.temperature_c=29;
    CHECK(et014_create(&hal,&cfg,&d)==ET014_OK); CHECK(d!=NULL); CHECK(f->calls==0); return d;
}
static void close_device(et014_t *d, fake_t *f) {
    CHECK(!f->active); if(f->begins) { CHECK(f->ends==f->begins); CHECK(f->power_lows>=f->begins); CHECK(f->last_low_step>f->last_end_step); }
    et014_destroy(d);
}
static void reset_observation(fake_t *f) {
    int64_t clock=f->clock; fake_init(f); f->clock=clock;
}
static void source_audit(fake_t *f, const uint8_t *old, const uint8_t *now, unsigned init, unsigned mode) {
    f->audit=f->source_audit=true; f->expected_init=init; f->expected_mode=mode;
    memcpy(f->source_old,old,ET014_FRAMEBUFFER_BYTES); memcpy(f->source_new,now,ET014_FRAMEBUFFER_BYTES);
}

static void test_waveform(void) {
    static const unsigned temps[]={2,5,8,11,15,20,25,29,32,37};
    static const unsigned counts[10][4]={{121,101,101,71},{121,61,61,51},{121,46,46,31},{121,46,46,25},{121,31,31,22},{121,31,31,15},{121,31,31,16},{121,31,31,15},{121,31,31,15},{121,31,31,15}};
    unsigned sum=0;
    CHECK(waveform_size==74472); CHECK(le16(0)==4); CHECK(le32(4)==waveform_size); CHECK(le16(8)==10 && le16(10)==4);
    for(size_t i=0;i<waveform_size;++i) if(i!=2 && i!=3) sum+=waveform[i];
    CHECK((unsigned)((-sum)&0xffff)==le16(2));
    for(unsigned t=0;t<10;++t) {
        CHECK(le16(48+t*20)==temps[t]);
        for(unsigned m=0;m<4;++m) {
            size_t off=mode_offset((int)temps[t],m);
            CHECK(frame_count((int)temps[t],m)==counts[t][m]);
            CHECK(off+(m?4+64*counts[t][m]:12)<=waveform_size);
            if(!m) { CHECK(le32(off+4)==0xffffffffu); CHECK(le16(off+8)==20 && le16(off+10)==3); }
        }
    }
    ++cases; puts("PASS exact supplier waveform shape/checksum/40 schedules");
}
static void test_arguments(void) {
    fake_t f; et014_t *d=NULL; fake_init(&f); et014_hal_t h=hal_for(&f); et014_config_t c;
    et014_config_default(&c); CHECK(et014_create(NULL,&c,&d)==ET014_ERR_ARGUMENT);
    CHECK(et014_create(&h,NULL,&d)==ET014_ERR_ARGUMENT);
    c.vcom_mv=-999; CHECK(et014_create(&h,&c,&d)==ET014_ERR_ARGUMENT);
    et014_config_default(&c); c.temperature_c=41; CHECK(et014_create(&h,&c,&d)==ET014_ERR_ARGUMENT);
    d=new_device(&f); CHECK(et014_set_temperature(d,-1)==ET014_ERR_ARGUMENT); CHECK(et014_set_temperature(d,41)==ET014_ERR_ARGUMENT);
    CHECK(et014_set_pixel(d,-1,0,0)==ET014_ERR_ARGUMENT); CHECK(et014_set_pixel(d,128,0,0)==ET014_ERR_ARGUMENT);
    CHECK(et014_set_pixel(d,0,296,0)==ET014_ERR_ARGUMENT); CHECK(et014_set_pixel(d,0,0,4)==ET014_ERR_ARGUMENT);
    CHECK(et014_fill(d,4)==ET014_ERR_ARGUMENT); CHECK(et014_get_pixel(d,-1,0)==ET014_WHITE);
    CHECK(et014_refresh(d,(et014_mode_t)90,NULL,false)==ET014_ERR_ARGUMENT);
    et014_rect_t bad={127,0,2,10}; CHECK(et014_refresh(d,ET014_GC4,&bad,false)==ET014_ERR_ARGUMENT);
    CHECK(f.begins==0); close_device(d,&f); ++cases; puts("PASS arguments rejected without hardware side effects");
}
static void test_probe_and_temperature(void) {
    fake_t f; et014_t *d=new_device(&f); uint8_t id=0; int temp=0;
    CHECK(et014_probe(d,&id)==ET014_OK && id==0x12); CHECK(f.power_count==0);
    CHECK(et014_read_temperature(d,&temp)==ET014_OK && temp==29); CHECK(f.power_count==0); close_device(d,&f);
    d=new_device(&f); f.id_bad=1; CHECK(et014_probe(d,&id)==ET014_ERR_ID); close_device(d,&f);
    d=new_device(&f); f.status_bad=1; CHECK(et014_read_temperature(d,&temp)!=ET014_OK); close_device(d,&f);
    ++cases; puts("PASS identity and sensor probe, no HV registers");
}
static void test_profile(void) {
    fake_t f; et014_t *d=new_device(&f); f.audit=true;
    CHECK(et014_profile(d,2)==ET014_OK); CHECK(f.profile); CHECK(f.power_count==0);
    CHECK(f.frames==2 && f.selected_lines==592); CHECK(f.triggers==1186);
    CHECK(et014_stats(d)->maximum_frame_us<=23000); CHECK(!et014_history_valid(d)); close_device(d,&f);
    ++cases; puts("PASS CS-high profile: 296 descending gates and final blank, no HV");
}
static void test_full_and_region(void) {
    fake_t f; et014_t *d=new_device(&f); uint8_t white[ET014_FRAMEBUFFER_BYTES]; memset(white,0xff,sizeof(white));
    for(int y=0;y<296;++y) for(int x=0;x<128;++x) CHECK(et014_set_pixel(d,x,y,(uint8_t)((x+y)%4))==ET014_OK);
    source_audit(&f,white,et014_framebuffer(d),121,1);
    CHECK(et014_refresh(d,ET014_AUTO,NULL,false)==ET014_OK); CHECK(et014_history_valid(d));
    CHECK(f.frames==152 && f.selected_lines==152*296); CHECK(f.checked_sources==152*296*32);
    { static const unsigned char rails[]={0,1,3,7,15,7,3,1,0}; CHECK(f.power_count==sizeof(rails)); CHECK(memcmp(f.power_values,rails,sizeof(rails))==0); }
    CHECK(memcmp(et014_framebuffer(d),et014_previous_framebuffer(d),sizeof(white))==0);
    CHECK(et014_stats(d)->init_frames==121 && et014_stats(d)->image_frames==31);
    CHECK(et014_stats(d)->initialized);
    et014_rect_t dirty; CHECK(!et014_dirty_bounds(d,&dirty));
    unsigned calls=f.calls; CHECK(et014_refresh(d,ET014_GC4,NULL,false)==ET014_OK); CHECK(f.calls==calls); CHECK(et014_stats(d)->skipped);
    CHECK(et014_set_pixel(d,3,5,3)==ET014_OK); CHECK(et014_set_pixel(d,90,205,0)==ET014_OK);
    CHECK(et014_dirty_bounds(d,&dirty)); CHECK(dirty.x==3 && dirty.y==5 && dirty.width==88 && dirty.height==201);
    uint8_t old[ET014_FRAMEBUFFER_BYTES], target[ET014_FRAMEBUFFER_BYTES];
    memcpy(old,et014_previous_framebuffer(d),sizeof(old)); memcpy(target,old,sizeof(target));
    target[5*32] = (uint8_t)((target[5*32]&0xfcu)|3u);
    et014_rect_t part={3,5,1,1}; reset_observation(&f); source_audit(&f,old,target,0,1);
    CHECK(et014_refresh(d,ET014_GC4,&part,false)==ET014_OK); CHECK(f.frames==31);
    CHECK(memcmp(target,et014_previous_framebuffer(d),sizeof(target))==0);
    CHECK(et014_dirty_bounds(d,&dirty)); CHECK(dirty.x==90 && dirty.y==205 && dirty.width==1 && dirty.height==1);
    CHECK(et014_stats(d)->changed_pixels==1); close_device(d,&f);
    ++cases; puts("PASS exact INIT/GC4 source bytes; no-op; narrow region commit and pending outside changes");
}
static void test_temperature_schedules(void) {
    for(int t=0;t<=40;++t) {
        fake_t f; et014_t *d=new_device(&f); CHECK(et014_set_temperature(d,t)==ET014_OK);
        CHECK(et014_refresh(d,ET014_GC4,NULL,true)==ET014_OK);
        CHECK(et014_stats(d)->waveform_temperature_c==(int)le16(bucket(t)));
        CHECK(et014_stats(d)->frames==121+frame_count(t,1)); close_device(d,&f);
    }
    ++cases; puts("PASS every integer temperature 0..40, nearest bucket and expected frame totals");
}
static void test_experimental_guards(void) {
    fake_t f; et014_t *d=new_device(&f);
    CHECK(et014_refresh(d,ET014_A2,NULL,false)==ET014_ERR_EXPERIMENTAL);
    CHECK(et014_refresh(d,ET014_GU4,NULL,false)==ET014_ERR_EXPERIMENTAL); CHECK(f.calls==0);
    CHECK(et014_set_experimental(d,true)==ET014_OK);
    CHECK(et014_refresh(d,ET014_A2,NULL,false)==ET014_ERR_STATE); CHECK(f.calls==0);
    CHECK(et014_fill(d,ET014_WHITE)==ET014_OK); CHECK(et014_refresh(d,ET014_GC4,NULL,true)==ET014_OK);
    CHECK(et014_set_pixel(d,4,7,ET014_DARK_GRAY)==ET014_OK);
    unsigned calls=f.calls; CHECK(et014_refresh(d,ET014_A2,NULL,false)==ET014_ERR_NOT_BINARY); CHECK(f.calls==calls);
    CHECK(et014_set_pixel(d,4,7,ET014_BLACK)==ET014_OK);
    reset_observation(&f); source_audit(&f,et014_previous_framebuffer(d),et014_framebuffer(d),0,3);
    CHECK(et014_refresh(d,ET014_A2,NULL,false)==ET014_OK); CHECK(f.frames==15); CHECK(et014_history_valid(d));
    close_device(d,&f); ++cases; puts("PASS experimental opt-in, A2 known binary history and exact 15-frame source (host only)");
}
static void assert_failed_safely(et014_t *d, fake_t *f, et014_err_t expected) {
    et014_err_t got=et014_refresh(d,ET014_GC4,NULL,true);
    CHECK(got==expected); CHECK(et014_last_error(d)==expected); CHECK(!et014_history_valid(d));
    CHECK(!f->active && f->ends==f->begins && f->power_lows>=f->begins);
}
static void test_failure_paths(void) {
    fake_t f; et014_t *d=new_device(&f); f.busy_stuck=1;
    assert_failed_safely(d,&f,ET014_ERR_TIMEOUT); CHECK(f.clock<2000000); close_device(d,&f);
    d=new_device(&f); f.busy_error=1; assert_failed_safely(d,&f,ET014_ERR_IO); close_device(d,&f);
    d=new_device(&f); f.not_ready=1; assert_failed_safely(d,&f,ET014_ERR_TIMEOUT); CHECK(f.clock<2000000); close_device(d,&f);
    d=new_device(&f); f.cancel_now=1; assert_failed_safely(d,&f,ET014_ERR_CANCELLED); close_device(d,&f);
    d=new_device(&f); f.audit=true; f.cancel_at_frame=1;
    assert_failed_safely(d,&f,ET014_ERR_CANCELLED); CHECK(f.frames==1); CHECK(!et014_stats(d)->initialized); close_device(d,&f);
    { static const int stages[]={0,1,3,7,15};
      for(unsigned s=0;s<sizeof(stages)/sizeof(stages[0]);++s) {
        d=new_device(&f); f.fail_reg=5; f.fail_value=stages[s];
        assert_failed_safely(d,&f,ET014_ERR_IO); CHECK(f.faults==1);
        for(unsigned i=0;i<f.power_count;++i) CHECK(f.power_values[i]<=stages[s]); close_device(d,&f);
      }
    }
    d=new_device(&f); f.fail_packet=18;
    assert_failed_safely(d,&f,ET014_ERR_IO); CHECK(f.faults==1); close_device(d,&f);
    d=new_device(&f); f.extra_us=100;
    assert_failed_safely(d,&f,ET014_ERR_TIMING); close_device(d,&f);
    d=new_device(&f); f.begin_fail=1; assert_failed_safely(d,&f,ET014_ERR_IO); close_device(d,&f);
    d=new_device(&f); f.end_fail=1; assert_failed_safely(d,&f,ET014_ERR_IO); close_device(d,&f);
    d=new_device(&f); f.power_low_fail=1; assert_failed_safely(d,&f,ET014_ERR_IO); close_device(d,&f);
    ++cases; puts("PASS bounded BUSY/ready timeout, GPIO error, cancellation, partial power rollback, IO/timing/cleanup faults");
}
static void test_failed_update_does_not_commit(void) {
    fake_t f; et014_t *d=new_device(&f); CHECK(et014_refresh(d,ET014_GC4,NULL,true)==ET014_OK);
    uint8_t saved[ET014_FRAMEBUFFER_BYTES]; memcpy(saved,et014_previous_framebuffer(d),sizeof(saved));
    CHECK(et014_fill(d,ET014_BLACK)==ET014_OK); reset_observation(&f); f.fail_packet=30;
    CHECK(et014_refresh(d,ET014_GC4,NULL,false)==ET014_ERR_IO); CHECK(!et014_history_valid(d));
    CHECK(memcmp(saved,et014_previous_framebuffer(d),sizeof(saved))==0);
    reset_observation(&f); CHECK(et014_refresh(d,ET014_GC4,NULL,false)==ET014_OK);
    CHECK(et014_stats(d)->init_frames==121); CHECK(et014_history_valid(d));
    CHECK(memcmp(et014_framebuffer(d),et014_previous_framebuffer(d),sizeof(saved))==0); close_device(d,&f);
    ++cases; puts("PASS failed update preserves committed bytes, invalidates trust, recovery forces INIT");
}

static void test_a2_pair(void) {
    fake_t f; et014_t *d=new_device(&f); uint8_t next[ET014_FRAMEBUFFER_BYTES];
    memset(next,0xcc,sizeof(next));
    CHECK(et014_a2_pair(d,next,sizeof(next),true)==ET014_ERR_EXPERIMENTAL); CHECK(f.begins==0);
    CHECK(et014_set_experimental(d,true)==ET014_OK);
    CHECK(et014_a2_pair(d,next,sizeof(next)-1,true)==ET014_ERR_ARGUMENT); CHECK(f.begins==0);
    CHECK(et014_fill(d,ET014_WHITE)==ET014_OK); f.audit=true;
    CHECK(et014_a2_pair(d,next,sizeof(next),true)==ET014_OK);
    CHECK(f.begins==1 && f.ends==1); CHECK(f.frames==121+31+15);
    CHECK(et014_history_valid(d)); CHECK(memcmp(et014_previous_framebuffer(d),next,sizeof(next))==0);
    CHECK(memcmp(et014_framebuffer(d),next,sizeof(next))==0); close_device(d,&f);
    ++cases; puts("PASS finite INIT/GC4 then A2 same-powered-session experiment, committed target (host only)");
}

int main(int argc, char **argv) {
    CHECK(argc==2); FILE *fp=fopen(argv[1],"rb"); CHECK(fp!=NULL);
    CHECK(fseek(fp,0,SEEK_END)==0); long length=ftell(fp); CHECK(length>0); waveform_size=(size_t)length;
    CHECK(fseek(fp,0,SEEK_SET)==0); waveform=malloc(waveform_size); CHECK(waveform!=NULL);
    CHECK(fread(waveform,1,waveform_size,fp)==waveform_size); CHECK(fclose(fp)==0);
    test_waveform(); test_arguments(); test_probe_and_temperature(); test_profile();
    test_full_and_region(); test_temperature_schedules(); test_experimental_guards();
    test_failure_paths(); test_failed_update_does_not_commit(); test_a2_pair();
    free(waveform); printf("HOST DRIVER PASS: %u cases, %u checks; no physical display claims\n",cases,checks); return 0;
}

/* A finite, single-owner sequence. Every panel call returns with power off. */
#include "showcase_task.h"
#include "et014_showcase_plan.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

static et014_t *device;
static et014_esp32_t *platform;
static showcase_log_t log_message;
static et014_gfx_t canvas;
static atomic_bool stop_requested;
static atomic_bool timing_passed;
static portMUX_TYPE status_lock=portMUX_INITIALIZER_UNLOCKED;
static showcase_snapshot_t published;
static uint64_t last_finish_ms;
static bool has_finished;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
static void publish(const showcase_snapshot_t *s)
{
    portENTER_CRITICAL(&status_lock);published=*s;portEXIT_CRITICAL(&status_lock);
}
void showcase_task_init(et014_t *d,et014_esp32_t *p,showcase_log_t logger)
{
    device=d;platform=p;log_message=logger;et014_gfx_init(&canvas,d);
    atomic_init(&stop_requested,false);atomic_init(&timing_passed,false);
    published=(showcase_snapshot_t){.state=SHOWCASE_IDLE,.temperature_c=ET014_TEMPERATURE_SENSOR};
}
void showcase_task_prepare(void) { atomic_store(&stop_requested,false); }
void showcase_task_stop(void)
{
    atomic_store(&stop_requested,true);et014_esp32_cancel(platform,true);
}
bool showcase_task_profile_passed(void) { return atomic_load(&timing_passed); }
showcase_snapshot_t showcase_task_snapshot(void)
{
    uint64_t current=now_ms();showcase_snapshot_t s;
    portENTER_CRITICAL(&status_lock);
    s=published;
    if(has_finished && current>=last_finish_ms && current-last_finish_ms<ET014_SHOWCASE_REPLAY_COOLDOWN_MS)
        s.remaining_cooldown_ms=ET014_SHOWCASE_REPLAY_COOLDOWN_MS-(unsigned)(current-last_finish_ms);
    portEXIT_CRITICAL(&status_lock);
    return s;
}
static et014_err_t hold(uint32_t duration_ms)
{
    uint64_t until=now_ms()+duration_ms;
    for(;;){
        if(atomic_load(&stop_requested))return ET014_ERR_CANCELLED;
        uint64_t current=now_ms();if(current>=until)break;
        uint64_t remaining=until-current;
        TickType_t ticks=pdMS_TO_TICKS(remaining<ET014_SHOWCASE_HOLD_POLL_MS?remaining:ET014_SHOWCASE_HOLD_POLL_MS);
        vTaskDelay(ticks?ticks:1);
    }
    return atomic_load(&stop_requested)?ET014_ERR_CANCELLED:ET014_OK;
}
static bool outside_matches(et014_rect_t r)
{
    const uint8_t *old=et014_previous_framebuffer(device);
    if(!old || !et014_history_valid(device))return false;
    for(int y=0;y<ET014_HEIGHT;++y)for(int x=0;x<ET014_WIDTH;++x){
        if(x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height)continue;
        unsigned shift=6-2*(x&3);
        if(et014_get_pixel(device,x,y)!=((old[y*ET014_STRIDE+x/4]>>shift)&3))return false;
    }
    return true;
}
et014_err_t showcase_task_run(void)
{
    if(!device || !platform || !log_message)return ET014_ERR_STATE;
    bool allowed;
    portENTER_CRITICAL(&status_lock);
    allowed=et014_showcase_replay_allowed(now_ms(),last_finish_ms,has_finished);
    portEXIT_CRITICAL(&status_lock);
    if(!allowed){log_message("SHOWCASE cooldown; wait 60s after finish\r\n");return ET014_ERR_BUSY;}
    if(!et014_showcase_plan_valid())return ET014_ERR_STATE;
    showcase_snapshot_t s={.state=SHOWCASE_RUNNING,.temperature_c=ET014_TEMPERATURE_SENSOR};
    et014_showcase_metrics_t metrics={0};
    et014_showcase_budget_t worst;et014_showcase_plan_worst_case(&worst);
    publish(&s);
    uint64_t start=now_ms();
    et014_err_t err=ET014_OK;
    atomic_store(&timing_passed,false);
    log_message("SHOWCASE START version=%s updates=12 max_a2=5 max_cleans=2 scan_budget=%lu holds=%lums\r\n",
        ET014_VERSION,(unsigned long)worst.total_scan_frames,(unsigned long)worst.total_hold_ms);
    if(atomic_load(&stop_requested)){err=ET014_ERR_CANCELLED;goto finish;}
    err=et014_profile(device,6);
    const et014_stats_t *profile=et014_stats(device);
    bool good=err==ET014_OK && profile->frames==6 && profile->maximum_frame_us<=21000 && profile->late_lines<=12;
    log_message("SHOWCASE PROFILE %s min=%lu max=%lu late=%lu\r\n",good?"PASS":"FAIL",
        (unsigned long)profile->minimum_frame_us,(unsigned long)profile->maximum_frame_us,(unsigned long)profile->late_lines);
    if(!good){if(err==ET014_OK)err=ET014_ERR_TIMING;goto finish;}
    atomic_store(&timing_passed,true);
    uint8_t id=0;err=et014_probe(device,&id);
    log_message("SHOWCASE ID=%02X result=%d\r\n",id,err);
    if(err!=ET014_OK)goto finish;
    err=et014_set_experimental(device,true);
    if(err!=ET014_OK)goto finish;

    for(size_t i=0;i<et014_showcase_plan_count();++i){
        const et014_showcase_step_t *step=et014_showcase_plan_step(i);
        if(atomic_load(&stop_requested)){err=ET014_ERR_CANCELLED;break;}
        if(!step || s.updates>=ET014_SHOWCASE_MAX_UPDATES || (step->mode==ET014_A2 && s.a2>=5) ||
            (step->force_clean && s.cleans>=2) || (i>0 && !et014_history_valid(device))){err=ET014_ERR_STATE;break;}
        if(step->sample_temp){
            int measured=0;err=et014_read_temperature(device,&measured);
            if(err!=ET014_OK)break;
            if(measured<0 || measured>40){err=ET014_ERR_TEMPERATURE;break;}
            err=et014_set_temperature(device,measured);
            if(err!=ET014_OK)break;
            metrics.temperature_c=measured;metrics.temperature_valid=true;s.temperature_c=measured;
            log_message("SHOWCASE TEMP=%d C source=panel_sensor uncalibrated\r\n",measured);
        }
        metrics.animation_index=step->animation_index;metrics.inverted=step->inverted;
        s.step=(unsigned)i+1;
        if(step->use_region){
            err=et014_showcase_motion_step(&canvas,&metrics);
            if(err==ET014_OK && !outside_matches(step->region))err=ET014_ERR_STATE;
        }else{
            metrics.counter=s.step;
            err=et014_showcase_draw(&canvas,step->page,&metrics,NULL);
        }
        if(err!=ET014_OK)break;
        publish(&s);
        log_message("SHOWCASE STEP %u/12 page=%s mode=%d clean=%d region=%d hold_ms=%lu\r\n",
            s.step,et014_showcase_page_name(step->page),step->mode,step->force_clean,step->use_region,
            (unsigned long)step->hold_after_ms);
        err=et014_refresh(device,step->mode,step->use_region?&step->region:NULL,step->force_clean);
        const et014_stats_t *stats=et014_stats(device);
        s.scans+=stats->frames;
        if(stats->init_frames)++s.cleans;
        if(!stats->skipped){
            ++s.updates;
            if(stats->mode==ET014_GC4)++s.gc4;
            else if(stats->mode==ET014_GU4)++s.gu4;
            else if(stats->mode==ET014_A2)++s.a2;
        }
        log_message("SHOWCASE RESULT step=%u result=%d frames=%lu init=%lu scan_us=%llu total_us=%llu changed=%lu temp=%d cleanup=%d\r\n",
            s.step,err,(unsigned long)stats->frames,(unsigned long)stats->init_frames,
            (unsigned long long)stats->scan_us,(unsigned long long)stats->elapsed_us,
            (unsigned long)stats->changed_pixels,stats->temperature_c,stats->cleanup_error);
        metrics.last_refresh_ms=(uint32_t)((stats->elapsed_us+500)/1000);
        if(s.scans>worst.total_scan_frames || s.cleans>2 || s.a2>5 || s.updates>12){if(err==ET014_OK)err=ET014_ERR_STATE;}
        publish(&s);
        if(err!=ET014_OK)break;
        err=hold(step->hold_after_ms);
        if(err!=ET014_OK)break;
    }
finish:
    /* No extra recovery refresh on cancellation/failure: core already shut HV off. */
    et014_set_experimental(device,false);
    et014_set_temperature(device,ET014_TEMPERATURE_SENSOR);
    s.result=err;s.state=err==ET014_OK?SHOWCASE_COMPLETE:(err==ET014_ERR_CANCELLED?SHOWCASE_STOPPED:SHOWCASE_FAILED);
    portENTER_CRITICAL(&status_lock);
    last_finish_ms=now_ms();has_finished=true;published=s;
    portEXIT_CRITICAL(&status_lock);
    log_message("SHOWCASE END state=%s result=%d updates=%u scans=%u gc4=%u gu4=%u a2=%u cleans=%u elapsed_ms=%llu power_off=%s\r\n",
        s.state==SHOWCASE_COMPLETE?"COMPLETE":s.state==SHOWCASE_STOPPED?"STOPPED":"FAILED",err,
        s.updates,s.scans,s.gc4,s.gu4,s.a2,s.cleans,(unsigned long long)(now_ms()-start),
        et014_stats(device)->cleanup_error==ET014_OK && et014_esp32_last_error(platform)==ESP_OK?"commands_completed":"attempted_unconfirmed");
    return err;
}

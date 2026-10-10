/* Single-owner console and finite autonomous UI showcase. No NVS writes. */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "et014.h"
#include "et014_gfx.h"
#include "et014_esp32.h"
#include "city_bitmap.h"
#include "showcase_task.h"

typedef struct {char command[128];uint8_t *frame;} job_t;
typedef struct {char text[128];size_t used;bool overflow;} line_t;
typedef struct {uint8_t *data;size_t used;uint32_t expected;int channel;int64_t deadline;} upload_t;
static et014_t *display;
static et014_esp32_t *port;
static et014_gfx_t gfx;
static QueueHandle_t jobs;
static SemaphoreHandle_t output_lock;
static atomic_bool working;
static atomic_int last_result;
static bool uart_ready,usb_ready,profile_passed;
static upload_t upload;
static bool autostart_pending;
static int64_t autostart_at;
static RTC_NOINIT_ATTR uint32_t showcase_boot_guard;
static RTC_NOINIT_ATTR uint32_t showcase_boot_guard_inverse;
#define SHOWCASE_BOOT_MAGIC UINT32_C(0x45504431)
#define USB_RX_BUFFER_SIZE 16384
_Static_assert(USB_RX_BUFFER_SIZE > ET014_FRAMEBUFFER_BYTES + 128,
               "USB RX must retain one full frame plus command framing");

static void reply(const char *format,...)
{
    char buffer[512];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    if(output_lock)xSemaphoreTake(output_lock,portMAX_DELAY);
    size_t len=strlen(buffer);
    if(uart_ready)uart_write_bytes(UART_NUM_0,buffer,len);
    if(usb_ready){
        size_t sent=0;int64_t deadline=esp_timer_get_time()+100000;
        while(sent<len && esp_timer_get_time()<deadline){
            int n=usb_serial_jtag_write_bytes(buffer+sent,len-sent,pdMS_TO_TICKS(10));
            if(n>0)sent+=(size_t)n;else vTaskDelay(1);
        }
    }
    if(output_lock)xSemaphoreGive(output_lock);
}
static uint32_t crc32(const uint8_t *bytes,size_t count)
{
    uint32_t crc=0xffffffff;
    while(count--){crc^=*bytes++;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    return ~crc;
}
static void help(void)
{
    reply("\r\nET014 DRIVER %s / ESP32-S3 / 128x296 / 4 gray\r\n",ET014_VERSION);
    reply("Finite UI showcase, 12 updates then static. PWR4 RST5 BUSY6 OEI7 CS10 MOSI11 SCK12 MISO13\r\n");
    reply("help | status | cancel | probe | temp | profile\r\n");
    reply("showcase run | showcase stop | showcase status (manual replay cooldown 60s)\r\n");
    reply("city | demo | fill 0..3 | invert | rotate 0/90/180/270 (draw only)\r\n");
    reply("refresh auto/gc4/clean/gu4/a2 [x y w h] | experimental on/off | a2pair\r\n");
    reply("load CRC32HEX then 9472 raw 2bpp bytes (native MSB first)\r\nDONE help result=0\r\n");
}
static void showcase_status(void)
{
    const char *names[]={"IDLE","RUNNING","COMPLETE","STOPPED","FAILED"};
    showcase_snapshot_t s=showcase_task_snapshot();
    reply("SHOWCASE state=%s auto_pending=%d step=%u/12 updates=%u scans=%u gc4=%u gu4=%u a2=%u cleans=%u temp=%d cooldown_ms=%u result=%d\r\n",
        names[s.state],autostart_pending,s.step,s.updates,s.scans,s.gc4,s.gu4,s.a2,s.cleans,
        s.temperature_c,s.remaining_cooldown_ms,s.result);
}
static void statistics(void)
{
    const et014_stats_t *s=et014_stats(display);
    reply("STATS history=%d mode=%d temp=%d bucket=%d frames=%lu init=%lu image=%lu scan=%llu us total=%llu us changed=%lu skip=%d cleanup=%d\r\n",
        et014_history_valid(display),(int)s->mode,s->temperature_c,s->waveform_temperature_c,
        (unsigned long)s->frames,(unsigned long)s->init_frames,(unsigned long)s->image_frames,
        (unsigned long long)s->scan_us,(unsigned long long)s->elapsed_us,
        (unsigned long)s->changed_pixels,s->skipped,(int)s->cleanup_error);
}
static et014_err_t demo(void)
{
    et014_gfx_rotation(&gfx,ET014_ROTATE_0);et014_gfx_clear(&gfx,ET014_WHITE);
    et014_err_t err=et014_gfx_text(&gfx,10,8,"ET014 DRIVER",0,-1,1);
    if(err)return err;
    et014_gfx_text(&gfx,10,20,"128 x 296",0,-1,1);
    for(unsigned g=0;g<4;++g){
        et014_gfx_rect(&gfx,8+28*g,40,28,42,g,true);
        char label[2]={(char)('0'+g),0};et014_gfx_text(&gfx,18+28*g,86,label,0,-1,1);
    }
    et014_gfx_rect(&gfx,7,39,114,44,0,false);
    et014_gfx_text(&gfx,10,103,"4 GRAY LEVELS",0,-1,1);
    for(int r=8;r<=40;r+=8)et014_gfx_circle(&gfx,64,164,r,(r/8)%4,false);
    et014_gfx_line(&gfx,18,164,110,164,0);et014_gfx_line(&gfx,64,118,64,210,0);
    et014_gfx_progress(&gfx,8,225,112,13,73,0,3);
    et014_gfx_text(&gfx,10,246,"HAL + GFX + STATE",0,-1,1);
    et014_gfx_text(&gfx,10,260,"NO CHANGE = SKIP",0,-1,1);
    et014_gfx_text(&gfx,10,278,"GC4 / GU4 / A2",0,-1,1);
    return ET014_OK;
}
static et014_err_t execute(job_t *job)
{
    if(!strcmp(job->command,"showcase run")){
        et014_err_t e=showcase_task_run();profile_passed=showcase_task_profile_passed();return e;
    }
    char word[24]={0};sscanf(job->command,"%23s",word);
    if(!strcmp(word,"load")){memcpy(et014_framebuffer(display),job->frame,ET014_FRAMEBUFFER_BYTES);return ET014_OK;}
    if(!strcmp(word,"status")){statistics();return ET014_OK;}
    if(!strcmp(word,"probe")){uint8_t id=0;et014_err_t e=et014_probe(display,&id);reply("ID %02X\r\n",id);return e;}
    if(!strcmp(word,"temp")){int t=0;et014_err_t e=et014_read_temperature(display,&t);reply("TEMP %d C (sensor, uncalibrated)\r\n",t);return e;}
    if(!strcmp(word,"profile")){
        et014_err_t e=et014_profile(display,6);const et014_stats_t *s=et014_stats(display);
        profile_passed=e==ET014_OK && s->frames==6 && s->maximum_frame_us<=21000 && s->late_lines<=12;
        reply("PROFILE %s min=%lu max=%lu late=%lu\r\n",profile_passed?"PASS":"FAIL",
            (unsigned long)s->minimum_frame_us,(unsigned long)s->maximum_frame_us,(unsigned long)s->late_lines);
        return profile_passed?ET014_OK:(e?e:ET014_ERR_TIMING);
    }
    if(!strcmp(word,"city")){
        et014_gfx_rotation(&gfx,ET014_ROTATE_0);
        return et014_gfx_bitmap1(&gfx,0,0,128,296,resolution_art,sizeof(resolution_art),16,0,3);
    }
    if(!strcmp(word,"demo"))return demo();
    if(!strcmp(word,"fill")){int gray=-1;char extra;if(sscanf(job->command,"fill %d %c",&gray,&extra)!=1 || gray<0 || gray>3)return ET014_ERR_ARGUMENT;return et014_fill(display,gray);}
    if(!strcmp(word,"invert"))return et014_gfx_invert(&gfx,0,0,et014_gfx_width(&gfx),et014_gfx_height(&gfx));
    if(!strcmp(word,"rotate")){
        int degree=-1;char extra;if(sscanf(job->command,"rotate %d %c",&degree,&extra)!=1 || degree<0 || degree>270 || degree%90)return ET014_ERR_ARGUMENT;
        return et014_gfx_rotation(&gfx,(et014_rotation_t)(degree/90));
    }
    if(!strcmp(word,"experimental")){
        if(!strcmp(job->command,"experimental on"))return et014_set_experimental(display,true);
        if(!strcmp(job->command,"experimental off"))return et014_set_experimental(display,false);
        return ET014_ERR_ARGUMENT;
    }
    if(!strcmp(word,"refresh")){
        if(!profile_passed)return ET014_ERR_TIMING;
        char mode[12]={0},extra;et014_rect_t rect={0};
        int n=sscanf(job->command,"refresh %11s %d %d %d %d %c",mode,&rect.x,&rect.y,&rect.width,&rect.height,&extra);
        if(n!=1 && n!=5)return ET014_ERR_ARGUMENT;
        et014_mode_t m;
        bool clean=!strcmp(mode,"clean");
        if(clean || !strcmp(mode,"gc4"))m=ET014_GC4;
        else if(!strcmp(mode,"auto"))m=ET014_AUTO;
        else if(!strcmp(mode,"gu4"))m=ET014_GU4;
        else if(!strcmp(mode,"a2"))m=ET014_A2;
        else return ET014_ERR_ARGUMENT;
        return et014_refresh(display,m,n==5?&rect:NULL,clean);
    }
    if(!strcmp(word,"a2pair")){
        if(!profile_passed)return ET014_ERR_TIMING;
        uint8_t *next=malloc(ET014_FRAMEBUFFER_BYTES);if(!next)return ET014_ERR_MEMORY;
        memcpy(next,et014_framebuffer(display),ET014_FRAMEBUFFER_BYTES);
        // 16x16 inversion at native x112/y40, preserving every other target pixel.
        for(unsigned y=40;y<56;++y)for(unsigned x=28;x<32;++x)next[y*32+x]^=0xff;
        et014_err_t e=et014_a2_pair(display,next,ET014_FRAMEBUFFER_BYTES,true);free(next);return e;
    }
    return ET014_ERR_ARGUMENT;
}
static void worker(void *unused)
{
    (void)unused;job_t job;
    for(;;){
        if(xQueueReceive(jobs,&job,portMAX_DELAY)!=pdTRUE)continue;
        et014_err_t result=execute(&job);
        if(job.frame){
            if(result==ET014_OK)reply("LOAD OK %08lX\r\n",(unsigned long)crc32(job.frame,ET014_FRAMEBUFFER_BYTES));
            free(job.frame);
        }
        if(!strncmp(job.command,"refresh",7) || !strcmp(job.command,"a2pair"))statistics();
        atomic_store(&last_result,result);
        if(result!=ET014_OK)reply("ERR %d %s; port=%s\r\n",(int)result,et014_error_string(result),esp_err_to_name(et014_esp32_last_error(port)));
        atomic_store(&working,false);
        reply("DONE %s result=%d\r\n",job.command,(int)result);
    }
}
static void submit(const char *text,uint8_t *frame)
{
    bool expected=false;
    if(!atomic_compare_exchange_strong(&working,&expected,true)){
        free(frame);reply("ERR BUSY; use status/cancel\r\n");return;
    }
    job_t job={.frame=frame};snprintf(job.command,sizeof(job.command),"%s",text);
    et014_esp32_cancel(port,false);
    if(!strcmp(text,"showcase run")){
        showcase_task_prepare();
        showcase_boot_guard=SHOWCASE_BOOT_MAGIC;
        showcase_boot_guard_inverse=~SHOWCASE_BOOT_MAGIC;
    }
    if(xQueueSend(jobs,&job,0)!=pdTRUE){free(frame);atomic_store(&working,false);reply("ERR queue full\r\n");}
}
static void command(const char *text,int channel)
{
    if(!strcmp(text,"help")){help();return;}
    if(!strcmp(text,"showcase status")){
        showcase_status();reply("DONE showcase status result=0\r\n");return;
    }
    if(!strcmp(text,"showcase stop")){
        autostart_pending=false;showcase_task_stop();
        reply("STOP requested; current frame completes before power-off\r\nDONE showcase stop result=0\r\n");return;
    }
    if(!strcmp(text,"cancel")){
        autostart_pending=false;showcase_task_stop();
        reply("CANCEL requested; frame boundary then staged power-off\r\nDONE cancel result=0\r\n");return;
    }
    if(!strcmp(text,"status") && atomic_load(&working)){
        showcase_status();
        reply("STATUS busy=1 last=%d\r\nDONE status result=0\r\n",atomic_load(&last_result));return;
    }
    if(strcmp(text,"status"))autostart_pending=false;
    if(!strncmp(text,"load ",5)){
        unsigned long crc;char extra;
        if(atomic_load(&working) || upload.data){reply("ERR BUSY\r\n");return;}
        if(strlen(text)!=13 || sscanf(text,"load %8lx %c",&crc,&extra)!=1){reply("ERR bad CRC32 header\r\n");return;}
        upload.data=malloc(ET014_FRAMEBUFFER_BYTES);
        if(!upload.data){reply("ERR no upload memory\r\n");return;}
        upload.used=0;upload.expected=(uint32_t)crc;upload.channel=channel;upload.deadline=esp_timer_get_time()+5000000;
        reply("READY 9472\r\n");return;
    }
    if(upload.data){reply("ERR upload in progress\r\n");return;}
    submit(text,NULL);
}
static void consume(line_t *line,uint8_t ch,int channel)
{
    if(upload.data && upload.channel==channel){
        upload.data[upload.used++]=ch;
        if(upload.used==ET014_FRAMEBUFFER_BYTES){
            uint8_t *data=upload.data;upload.data=NULL;
            if(crc32(data,ET014_FRAMEBUFFER_BYTES)!=upload.expected){free(data);reply("ERR image CRC mismatch; framebuffer unchanged\r\n");}
            else submit("load",data);
        }
        return;
    }
    if(ch=='\r')return; // Headers accept LF or CRLF; raw payload keeps every byte.
    if(ch=='\n'){
        if(line->overflow)reply("ERR line too long\r\n");
        else if(line->used){line->text[line->used]=0;command(line->text,channel);}
        line->used=0;line->overflow=false;
    }else if(ch>=32 && ch<=126){
        if(line->used<sizeof(line->text)-1)line->text[line->used++]=(char)ch;else line->overflow=true;
    }
}
void app_main(void)
{
    output_lock=xSemaphoreCreateMutex();
    uart_config_t uc={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    if(uart_param_config(UART_NUM_0,&uc)==ESP_OK && uart_set_pin(UART_NUM_0,43,44,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)==ESP_OK)
        uart_ready=uart_driver_install(UART_NUM_0,1024,0,0,NULL,0)==ESP_OK;
    // Native USB can burst an entire frame before this task gets scheduled.
    // IDF drops ISR bytes if the RX ring is full; size it for the full frame.
    usb_serial_jtag_driver_config_t usb={.rx_buffer_size=USB_RX_BUFFER_SIZE,.tx_buffer_size=8192};
    usb_ready=usb_serial_jtag_driver_install(&usb)==ESP_OK;
    et014_esp32_pins_t pins;et014_esp32_pins_default(&pins);et014_hal_t hal;
    et014_config_t config;et014_config_default(&config);
    if(et014_esp32_create(&pins,&port,&hal)!=ESP_OK || et014_create(&hal,&config,&display)!=ET014_OK){
        reply("ERR initialization\r\n");et014_esp32_destroy(port);return;
    }
    et014_gfx_init(&gfx,display);jobs=xQueueCreate(1,sizeof(job_t));
    showcase_task_init(display,port,reply);
    atomic_init(&working,false);atomic_init(&last_result,0);
    if(!jobs || xTaskCreatePinnedToCore(worker,"et014_worker",8192,NULL,4,NULL,1)!=pdPASS){
        reply("ERR worker allocation\r\n");if(jobs)vQueueDelete(jobs);et014_destroy(display);et014_esp32_destroy(port);return;
    }
    esp_reset_reason_t reason=esp_reset_reason();
    bool fault_reset=reason==ESP_RST_PANIC || reason==ESP_RST_INT_WDT || reason==ESP_RST_TASK_WDT ||
        reason==ESP_RST_WDT || reason==ESP_RST_BROWNOUT || reason==ESP_RST_PWR_GLITCH || reason==ESP_RST_CPU_LOCKUP;
    bool already_ran=showcase_boot_guard==SHOWCASE_BOOT_MAGIC && showcase_boot_guard_inverse==~SHOWCASE_BOOT_MAGIC;
    autostart_pending=!fault_reset && (reason==ESP_RST_POWERON || !already_ran);
    autostart_at=esp_timer_get_time()+10000000;
    help();reply("SHOWCASE auto_pending=%d reset_reason=%d startup_delay=10s; stop with showcase stop\r\n",autostart_pending,reason);
    line_t uart_line={0},usb_line={0};uint8_t bytes[256];
    for(;;){
        int n=uart_ready?uart_read_bytes(UART_NUM_0,bytes,sizeof(bytes),0):0;
        for(int i=0;i<n;++i)consume(&uart_line,bytes[i],0);
        n=usb_ready?usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0):0;
        for(int i=0;i<n;++i)consume(&usb_line,bytes[i],1);
        if(upload.data && esp_timer_get_time()>=upload.deadline){
            size_t received=upload.used;free(upload.data);upload.data=NULL;
            reply("ERR upload timeout received=%u expected=%u; framebuffer unchanged\r\n",
                  (unsigned)received,(unsigned)ET014_FRAMEBUFFER_BYTES);
        }
        if(autostart_pending && !atomic_load(&working) && !upload.data && esp_timer_get_time()>=autostart_at){
            autostart_pending=false;submit("showcase run",NULL);
        }
        vTaskDelay(1);
    }
}

/* ESP32-S3 port, adapted from the physically accepted v0.6 transport. */
#include <stdlib.h>
#include <stdatomic.h>
#include "et014_esp32.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "epd_fast_spi_s3.h"
#if ESP_IDF_VERSION_MAJOR != 5 || ESP_IDF_VERSION_MINOR != 5
#error "Audit the private SPI FIFO path before using an IDF version other than 5.5"
#endif
#if CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240
#error "This port requires 240 MHz and a successful CS-high profile"
#endif

struct et014_esp32 {
    et014_esp32_pins_t pins;
    spi_device_handle_t spi;
    bool bus, acquired, profile, fast, fault;
    uint32_t hz;
    int64_t last_yield;
    atomic_bool cancel;
    esp_err_t error;
};
static et014_err_t result(et014_esp32_t *p, esp_err_t e)
{ if (e != ESP_OK) p->error=e; return e == ESP_OK ? ET014_OK : ET014_ERR_IO; }
static void first(esp_err_t *out, esp_err_t e) { if (*out==ESP_OK && e!=ESP_OK) *out=e; }
static esp_err_t output(int pin,int level)
{
    esp_err_t e=gpio_set_level(pin,level);
    if(e!=ESP_OK)return e;
    gpio_config_t cfg={.pin_bit_mask=1ULL<<pin,.mode=GPIO_MODE_OUTPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    return gpio_config(&cfg);
}
static esp_err_t hold(et014_esp32_t *p)
{
    int out[]={p->pins.pwr,p->pins.reset,p->pins.oei,p->pins.cs,p->pins.mosi,p->pins.sck};
    esp_err_t e=ESP_OK;
    for(unsigned i=0;i<sizeof(out)/sizeof(out[0]);++i)first(&e,output(out[i],0));
    gpio_config_t cfg={.pin_bit_mask=(1ULL<<p->pins.busy)|(1ULL<<p->pins.miso),
        .mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_DISABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    first(&e,gpio_config(&cfg));
    return e;
}
static et014_err_t end(void *user)
{
    et014_esp32_t *p=user;
    esp_err_t e=gpio_set_level(p->pins.reset,0);
    esp_rom_delay_us(2);
    if(p->spi){
        if(p->acquired){spi_device_release_bus(p->spi);p->acquired=false;}
        esp_err_t r=spi_bus_remove_device(p->spi);first(&e,r);if(r==ESP_OK)p->spi=NULL;
    }
    if(p->bus && !p->spi){esp_err_t r=spi_bus_free(SPI2_HOST);first(&e,r);if(r==ESP_OK)p->bus=false;}
    first(&e,output(p->pins.cs,0));first(&e,output(p->pins.mosi,0));first(&e,output(p->pins.sck,0));
    first(&e,gpio_set_level(p->pins.oei,0));first(&e,gpio_pullup_dis(p->pins.miso));
    esp_rom_delay_us(40);first(&e,gpio_set_level(p->pins.pwr,0));
    p->fast=false;p->profile=false;p->fault=(e!=ESP_OK);
    return result(p,e);
}
static et014_err_t begin(void *user,bool profile)
{
    et014_esp32_t *p=user;
    if(p->spi || p->bus || p->acquired)return ET014_ERR_BUSY;
    p->error=ESP_OK;p->fault=false;p->profile=profile;p->fast=false;p->last_yield=esp_timer_get_time();
    esp_err_t e=hold(p);
    if(e!=ESP_OK)goto fail;
    if(profile){e=output(p->pins.cs,1);if(e!=ESP_OK)goto fail;}
    else{
        esp_rom_delay_us(1000);
        e=gpio_set_level(p->pins.pwr,1);if(e!=ESP_OK)goto fail;
        e=gpio_set_level(p->pins.cs,1);if(e!=ESP_OK)goto fail;
        esp_rom_delay_us(14000);
        e=gpio_set_level(p->pins.reset,1);if(e!=ESP_OK)goto fail;esp_rom_delay_us(5000);
        e=gpio_set_level(p->pins.reset,0);if(e!=ESP_OK)goto fail;esp_rom_delay_us(5000);
        e=gpio_set_level(p->pins.reset,1);if(e!=ESP_OK)goto fail;esp_rom_delay_us(5000);
    }
    spi_bus_config_t bus={.mosi_io_num=p->pins.mosi,.miso_io_num=p->pins.miso,.sclk_io_num=p->pins.sck,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=64};
    e=spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_DISABLED);if(e!=ESP_OK)goto fail;p->bus=true;
    spi_device_interface_config_t dev={.clock_speed_hz=500000,.mode=3,
        .spics_io_num=profile?-1:p->pins.cs,.queue_size=1,.cs_ena_pretrans=1,.cs_ena_posttrans=1};
    e=spi_bus_add_device(SPI2_HOST,&dev,&p->spi);if(e!=ESP_OK)goto fail;
    e=gpio_pullup_en(p->pins.miso);if(e!=ESP_OK)goto fail;
    /* IDF 5.5 rejects finite acquire timeouts. This freshly created SPI2 bus
     * has only our private device and no queued transactions, so there is no
     * competing owner to wait for. Applications must not share this bus. */
    e=spi_device_acquire_bus(p->spi,portMAX_DELAY);if(e!=ESP_OK)goto fail;p->acquired=true;
    p->hz=500000;return ET014_OK;
fail:
    p->error=e;end(p);p->error=e;return ET014_ERR_IO;
}
static et014_err_t transfer(void *user,const uint8_t *tx,uint8_t *rx,size_t n,uint32_t hz)
{
    et014_esp32_t *p=user;
    if(!p->spi || !p->acquired || p->fault || !tx || !n || n>64 || !hz || hz>20000000)
        return ET014_ERR_IO;
    esp_err_t e;
    if(p->fast && !rx && hz==20000000 && p->hz==hz)e=epd_fast_spi2_write(tx,n);
    else{
        p->fast=false;
        spi_transaction_t tr={.length=n*8,.tx_buffer=tx,.rx_buffer=rx,.override_freq_hz=hz==p->hz?0:hz};
        e=spi_device_polling_transmit(p->spi,&tr);
        if(e==ESP_OK){p->hz=hz;p->fast=hz==20000000 && !rx;}
    }
    if(e!=ESP_OK){p->fault=true;p->fast=false;}
    return result(p,e);
}
static et014_err_t power_low(void *user)
{et014_esp32_t *p=user;return result(p,gpio_set_level(p->pins.pwr,0));}
static int busy(void *user)
{et014_esp32_t *p=user;return p->profile?0:gpio_get_level(p->pins.busy);}
static int64_t now_us(void *user){(void)user;return esp_timer_get_time();}
static void delay_us(void *user,uint32_t us){(void)user;esp_rom_delay_us(us);}
static void delay_ms(void *user,uint32_t ms){(void)user;vTaskDelay(pdMS_TO_TICKS(ms));}
static bool cancelled(void *user){return atomic_load(&((et014_esp32_t *)user)->cancel);}
static void yield_frame(void *user)
{
    et014_esp32_t *p=user;
    int64_t now=esp_timer_get_time();
    // Gate is reset between frames; let idle tasks run every ~0.3s.
    if(now-p->last_yield>=300000){vTaskDelay(1);p->last_yield=esp_timer_get_time();}
}
void et014_esp32_pins_default(et014_esp32_pins_t *p)
{if(p)*p=(et014_esp32_pins_t){.pwr=4,.reset=5,.busy=6,.oei=7,.cs=10,.mosi=11,.sck=12,.miso=13};}
esp_err_t et014_esp32_create(const et014_esp32_pins_t *pins,et014_esp32_t **port,et014_hal_t *hal)
{
    if(!pins || !port || !hal)return ESP_ERR_INVALID_ARG;
    *port=NULL;
    int all[]={pins->pwr,pins->reset,pins->busy,pins->oei,pins->cs,pins->mosi,pins->sck,pins->miso};
    for(unsigned i=0;i<8;++i){
        if(!GPIO_IS_VALID_GPIO(all[i]) || ((i!=2 && i!=7) && !GPIO_IS_VALID_OUTPUT_GPIO(all[i])))return ESP_ERR_INVALID_ARG;
        for(unsigned j=0;j<i;++j)if(all[i]==all[j])return ESP_ERR_INVALID_ARG;
    }
    et014_esp32_t *p=calloc(1,sizeof(*p));if(!p)return ESP_ERR_NO_MEM;
    p->pins=*pins;atomic_init(&p->cancel,false);
    *hal=(et014_hal_t){.user=p,.begin=begin,.end=end,.power_low=power_low,.transfer=transfer,
        .busy=busy,.now_us=now_us,.delay_us=delay_us,.delay_ms=delay_ms,.cancelled=cancelled,.yield=yield_frame};
    *port=p;return ESP_OK;
}
void et014_esp32_destroy(et014_esp32_t *p)
{if(p){if(p->spi || p->bus)end(p);free(p);}}
void et014_esp32_cancel(et014_esp32_t *p,bool value){if(p)atomic_store(&p->cancel,value);}
esp_err_t et014_esp32_last_error(const et014_esp32_t *p){return p?p->error:ESP_ERR_INVALID_ARG;}

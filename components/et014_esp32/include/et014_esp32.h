#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "et014.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct et014_esp32 et014_esp32_t;
typedef struct {
    int pwr, reset, busy, oei, cs, mosi, sck, miso;
} et014_esp32_pins_t;
void et014_esp32_pins_default(et014_esp32_pins_t *pins);
/* SPI2 exclusively owned during operations. ESP32-S3, CPU240MHz, IDF5.5.
 * create allocates only; begin/end callbacks own all GPIO/SPI side effects. */
esp_err_t et014_esp32_create(const et014_esp32_pins_t *pins, et014_esp32_t **port, et014_hal_t *hal);
void et014_esp32_destroy(et014_esp32_t *port);
void et014_esp32_cancel(et014_esp32_t *port, bool cancelled);
esp_err_t et014_esp32_last_error(const et014_esp32_t *port);
#ifdef __cplusplus
}
#endif

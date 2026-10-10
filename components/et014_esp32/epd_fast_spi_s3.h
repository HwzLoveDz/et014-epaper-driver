#pragma once

/* Experimental ESP-IDF 5.5 / ESP32-S3 CPU-FIFO write-only fast path.
 * Private LL API: tied to this IDF version, not a portable public driver.
 * Requires SPI2_HOST acquired by this task, DMA disabled, no queued traffic,
 * and a just-completed PUBLIC polling WRITE transaction at the desired clock.
 * The priming transaction must establish Mode 3, single data line, no command/
 * address/dummy phases, hardware CS choice + setup/hold and no KEEP_ACTIVE.
 * Do NOT use after a read or clock change without re-priming through the public
 * driver; invalidate your fast-ready flag on any other SPI operation.
 * No caller-supplied padding is required: final FIFO word is copied safely.
 */
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_cpu.h"
#include "hal/spi_ll.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This fast path is audited only for ESP32-S3 / IDF 5.5"
#endif

static inline __attribute__((always_inline)) esp_err_t
epd_fast_spi2_write(const uint8_t *tx, size_t bytes)
{
    if (tx == NULL || bytes == 0 || bytes > 64)
        return ESP_ERR_INVALID_ARG;

    spi_dev_t *hw = SPI_LL_GET_HW(SPI2_HOST);
    if (spi_ll_get_running_cmd(hw))
        return ESP_ERR_INVALID_STATE;

    /* This fixed cycle budget bounds hangs without calling the OS in the hot
     * poll loop: 1 ms at 240 MHz, longer at lower CPU speeds. Normal 49-byte
     * writes at 20 MHz take 19.6 us plus CS setup/hold and software overhead. */
    const uint32_t timeout_cycles = 240000u;
    const uint32_t started = esp_cpu_get_cycle_count();

    spi_ll_clear_int_stat(hw);
    spi_ll_set_mosi_bitlen(hw, bytes * 8);
    spi_ll_enable_mosi(hw, 1);
    spi_ll_enable_miso(hw, 0);

    size_t words = bytes / 4;
    for (size_t i = 0; i < words; ++i) {
        const uint8_t *p = tx + 4 * i;
        /* Byte packing is safe for unaligned input and avoids 12 out-of-line
         * memcpy calls with this project's -fno-builtin-memcpy setting. */
        uint32_t word = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        hw->data_buf[i] = word;
    }
    if (bytes % 4) {
        uint32_t word = 0;
        const uint8_t *p = tx + 4 * words;
        for (size_t i = 0; i < bytes % 4; ++i)
            word |= (uint32_t)p[i] << (8 * i);
        hw->data_buf[words] = word;
    }

    /* Same apply/start sequence as spi_hal_user_start, with bounded waits.
     * CS hardware/mode/clock registers are deliberately not touched. */
    hw->cmd.update = 1;
    while (hw->cmd.update) {
        if ((uint32_t)(esp_cpu_get_cycle_count() - started) > timeout_cycles)
            return ESP_ERR_TIMEOUT;
    }
    spi_ll_user_start(hw);
    while (!spi_ll_usr_is_done(hw)) {
        if ((uint32_t)(esp_cpu_get_cycle_count() - started) > timeout_cycles)
            return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/* Integration pseudocode (not automatically enabled):
 *
 * if (fast_ready && hz == 20000000 && spi_hz == hz && rx == NULL)
 *     return epd_fast_spi2_write(tx,count);
 * fast_ready = false;
 * err = spi_device_polling_transmit(epd,&tr);
 * if (err == ESP_OK) {
 *     spi_hz = hz;
 *     fast_ready = (hz == 20000000 && rx == NULL);
 * }
 *
 * First establish timing with physical CS disconnected/high. In the physical
 * run hardware CS must use the same Mode3 setup/hold settings. Do not release
 * the acquired bus until a transfer is finished. A TIMEOUT requires fault
 * cleanup; do not continue issuing register writes to an in-flight peripheral.
 */

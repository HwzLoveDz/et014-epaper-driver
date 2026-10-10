idf_component_register(SRCS "et014_esp32.c" INCLUDE_DIRS "include"
    REQUIRES et014 esp_driver_gpio esp_driver_spi esp_timer)

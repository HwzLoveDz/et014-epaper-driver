idf_component_register(SRCS "main.c" "showcase_task.c" INCLUDE_DIRS "."
    REQUIRES et014 et014_esp32 et014_showcase esp_driver_uart esp_driver_usb_serial_jtag esp_timer esp_system)

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_adc
        esp_driver_dac
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_rmt
        esp_driver_spi
        esp_event
        esp_http_server
        esp_lcd
        esp_netif
        esp_timer
        esp_wifi
        mdns
        nvs_flash
        spiffs
)

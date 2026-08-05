# Main component CMakeLists.txt for ESP32-S3 立创实战派
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer esp_lcd driver pca9557)

# 🚀 Main 组件 CMakeLists.txt
idf_component_register(SRCS "main.c" "garden_nav.c" "garden_page.c" "garden_focus.c"
                    "garden_ai.c" "garden_device.c" "garden_album.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_timer esp_lcd_axs15260 esp_lcd driver)

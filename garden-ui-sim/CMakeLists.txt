cmake_minimum_required(VERSION 3.16)
project(garden-ui-sim C)
set(CMAKE_C_STANDARD 11)

add_subdirectory(lvgl)

find_package(SDL2 REQUIRED)

add_executable(garden-sim
    main.c
    garden_ui.c
)

target_compile_definitions(garden-sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(garden-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(garden-sim PRIVATE lvgl SDL2::SDL2main SDL2::SDL2)

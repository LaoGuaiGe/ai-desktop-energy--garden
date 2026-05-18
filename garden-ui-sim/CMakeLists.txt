cmake_minimum_required(VERSION 3.16)
project(garden-ui-sim C)
set(CMAKE_C_STANDARD 11)

set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)

set(LVGL_MANAGED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lvgl9.5-demo/managed_components/lvgl__lvgl")
if(EXISTS "${LVGL_MANAGED_DIR}/CMakeLists.txt")
    add_subdirectory("${LVGL_MANAGED_DIR}" lvgl)
else()
    add_subdirectory(lvgl)
endif()

get_target_property(LVGL_SOURCES lvgl SOURCES)
if(LVGL_SOURCES)
    list(FILTER LVGL_SOURCES EXCLUDE REGEX ".*[/\\\\]draw[/\\\\]sw[/\\\\]blend[/\\\\]helium[/\\\\].*")
    set_target_properties(lvgl PROPERTIES SOURCES "${LVGL_SOURCES}")
endif()

find_package(SDL2 REQUIRED)

add_executable(garden-sim
    main.c
    garden_ui.c
)

target_compile_definitions(garden-sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(garden-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(garden-sim PRIVATE lvgl SDL2::SDL2main SDL2::SDL2)

add_executable(garden-focus-sim
    focus_sim_main.c
    sim_nav_stub.c
    ../lvgl9.5-demo/main/garden_focus.c
)

target_compile_definitions(garden-focus-sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(garden-focus-sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl9.5-demo/main
)
target_link_libraries(garden-focus-sim PRIVATE lvgl SDL2::SDL2main SDL2::SDL2)

add_executable(garden-pages-sim
    pages_sim_main.c
    ../lvgl9.5-demo/main/garden_nav.c
    ../lvgl9.5-demo/main/garden_page.c
    ../lvgl9.5-demo/main/garden_focus.c
    ../lvgl9.5-demo/main/garden_ai.c
    ../lvgl9.5-demo/main/garden_device.c
    ../lvgl9.5-demo/main/garden_album.c
)

target_compile_definitions(garden-pages-sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(garden-pages-sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl9.5-demo/main
)
target_link_libraries(garden-pages-sim PRIVATE lvgl SDL2::SDL2main SDL2::SDL2)

idf_component_register(
    SRCS
        "app_main.c"
        "board.c"
        "gb_core.c"
        "gb_player.c"
        "gb_ppu.c"
        "game.c"
        "storage.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_lcd
        esp_timer
        fatfs
        sdmmc
)

set_source_files_properties(
    "gb_core.c"
    "gb_ppu.c"
    PROPERTIES COMPILE_OPTIONS "-O3"
)

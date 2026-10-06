idf_component_register(SRCS "epaper.c" "uc8151_bc.c" "epaper_paint.c"
                       INCLUDE_DIRS "include" "."
                       PRIV_REQUIRES driver esp_timer)

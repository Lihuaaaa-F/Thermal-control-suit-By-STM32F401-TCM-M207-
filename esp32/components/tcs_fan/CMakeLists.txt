idf_component_register(SRCS "fan_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_rmt tcs_core)

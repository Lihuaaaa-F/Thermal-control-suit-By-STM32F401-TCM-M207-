idf_component_register(SRCS "fan_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver espressif__led_strip tcs_core)

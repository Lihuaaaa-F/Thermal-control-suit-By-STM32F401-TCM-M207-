idf_component_register(SRCS "m207_proto.c" "m207_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_timer tcs_core)

idf_component_register(SRCS "proto.c" "comm.c" "link_task.c"
                       INCLUDE_DIRS "include" "."
                       REQUIRES driver esp_timer)

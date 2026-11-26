idf_component_register(SRCS "proto.c" "comm.c"
                            "link_task.c" "link_dispatch.c" "link_registry.c"
                            "link_demo.c" "link_selftest.c" "link_send.c" "link_status.c"
                       INCLUDE_DIRS "include" "."
                       REQUIRES driver esp_timer)

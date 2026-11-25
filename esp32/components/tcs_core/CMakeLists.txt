idf_component_register(SRCS "param_registry.c" "control.c" "control_service.c" "nvs_store.c" "post_pure.c"
                               "boot_record.c" "svc_status.c"
                       INCLUDE_DIRS "include" "."
                       REQUIRES nvs_flash tcs_link esp_timer)

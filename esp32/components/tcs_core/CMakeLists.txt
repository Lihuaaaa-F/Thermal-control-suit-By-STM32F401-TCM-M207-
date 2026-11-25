idf_component_register(SRCS "param_registry.c" "control.c" "control_service.c" "nvs_store.c" "post_pure.c"
                       "control_manual.c" "link_loss_guard.c"
                       INCLUDE_DIRS "include" "."
                       REQUIRES nvs_flash tcs_link esp_timer)

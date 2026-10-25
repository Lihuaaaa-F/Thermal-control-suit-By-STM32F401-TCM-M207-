idf_component_register(SRCS "param_registry.c" "control.c" "nvs_store.c" "post_pure.c"
                       INCLUDE_DIRS "include" "."
                       REQUIRES nvs_flash tcs_link)

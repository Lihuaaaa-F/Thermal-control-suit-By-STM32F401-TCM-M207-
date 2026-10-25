idf_component_register(SRCS "main.c"
                       
                       PRIV_REQUIRES nvs_flash esp_netif esp_event esp_wifi
                                     esp_timer app_update esp_partition
                                     tcs_link tcs_wifi tcs_web tcs_core tcs_m207)

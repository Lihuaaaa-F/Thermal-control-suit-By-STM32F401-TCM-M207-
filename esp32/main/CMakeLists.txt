idf_component_register(SRCS "main.c" "tcs_console.c" "r1e_probe.c" "tcs_snap.c"

                       PRIV_REQUIRES nvs_flash esp_netif esp_event esp_wifi
                                     esp_timer app_update esp_partition console
                                     esp_driver_usb_serial_jtag
                                     tcs_link tcs_wifi tcs_web tcs_core tcs_m207 tcs_tmp117 tcs_fan)

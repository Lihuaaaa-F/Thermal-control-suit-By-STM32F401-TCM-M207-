idf_component_register(SRCS "http_server.c" "websocket.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server esp_timer json tcs_link tcs_core tcs_m207)
target_add_binary_data(${COMPONENT_LIB} "web_files/index.html.gz" EMBED_FILES)
target_add_binary_data(${COMPONENT_LIB} "web_files/app.js.gz" EMBED_FILES)
target_add_binary_data(${COMPONENT_LIB} "web_files/style.css.gz" EMBED_FILES)
target_add_binary_data(${COMPONENT_LIB} "web_files/chart.umd.min.js.gz" EMBED_FILES)

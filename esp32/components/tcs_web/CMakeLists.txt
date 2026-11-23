idf_component_register(SRCS "http_server.c" "websocket.c" "web_status.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server esp_timer json tcs_link tcs_core tcs_m207)
# 网页静态资源（gzip 内嵌；改源后重压：gzip -9 -n -c <src> > <src>.gz，门禁 project_tcs/tools/web_gz_check.py）
foreach(asset index.html app.js style.css chart.js)
    target_add_binary_data(${COMPONENT_LIB} "web_files/${asset}.gz" EMBED_FILES)
endforeach()

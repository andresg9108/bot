idf_component_register(SRCS "agBLE.c" "agBLE_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES bt freertos)

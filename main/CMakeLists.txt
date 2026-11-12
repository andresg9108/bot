idf_component_register(SRCS "agbot.c" "motor_control.c" "motor_motion.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES agBLE esp_driver_gpio esp_driver_ledc freertos nvs_flash)

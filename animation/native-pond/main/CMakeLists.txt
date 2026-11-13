idf_component_register(
    SRCS "pond_main.c" "pond_scene.c" "pond_display.c" "pond_audio.c"
    INCLUDE_DIRS "." "../pixel-fish"
    REQUIRES esp-mosaico-bsp esp_timer app_update esp_app_format esp_tinyusb esp_driver_i2c esp_hw_support
    EMBED_FILES "koi_poses.bin" "assets/pond_sunny.mp3" "assets/pond_rain.mp3")
target_compile_definitions(${COMPONENT_LIB} PRIVATE KOI_NATIVE_FIRMWARE=1)

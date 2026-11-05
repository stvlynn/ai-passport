# Astra Monitor 应用源文件。基线硬件测试演示(main.c、demo_*.c、ui_pixel*)不参与构建,
# 仅为宿主测试与参考保留;见 app_main.c 顶部说明。
idf_component_register(
    SRCS "app_main.c"
         "app_nav.c"
         "app_net.c"
         "app_portal.c"
         "portal_proto.c"
         "app_settings.c"
         "app_audio.c"
         "app_grok.c"
         "grok_proto.c"
         "app_console.c"
         "gh_model.c"
         "lynn_anim.c"
         "lynn_stage.c"
         "lynn_stage_data.c"
         "gh_parse.c"
         "gh_fetch.c"
         "sound_synth.c"
         "ui.c"
         "ui_assets.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_driver_usb_serial_jtag esp_event esp_http_client esp_http_server
             esp_netif esp_timer esp_wifi json mbedtls nvs_flash
)

# 像素字体由 tools/gen_app_fonts.py 生成,源文件放在仓库根 assets/fonts/。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ui_font_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ui_font_24.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ui_font_36.c"
)

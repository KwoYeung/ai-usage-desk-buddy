if(IDF_VERSION_MAJOR LESS 6)
    set(legacy_json_component json)
endif()

idf_component_register(
    SRCS "buddy_orchestrator.c" "main.c"
         "buddy_line.c"
         "buddy_ble.c"
         "buddy_ble_store.c"
         "buddy_ble_lifecycle.c"
         "buddy_app_logic.c"
         "buddy_protocol.c"
         "buddy_state.c"
         "buddy_settings.c"
         "buddy_character.c"
        "buddy_ui.c"
        "buddy_usage.c"
        "buddy_deepseek.c"
        "buddy_alert.c"
        "buddy_sound.c"
        "buddy_lan.c"
        "buddy_lan_pair.c"
        "buddy_pair_gate.c"
        "buddy_lan_filter.c"
        "buddy_font_zh_14.c"
         "buddy_font_zh_extra_14.c"
        "buddy_font_zh_16.c"
        "buddy_font_currency_20.c"
        "buddy_i4.c"
        "buddy_sprite.c"
        "buddy_text_layout.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_usb_serial_jtag bsp bt esp_hw_support esp_timer ${legacy_json_component} nvs_flash esp_wifi esp_netif esp_event lwip mbedtls esp_http_server
    EMBED_FILES "notification_09.pcm" "notification_010.pcm" "charge.pcm"
    EMBED_TXTFILES "lan_setup.html"
)

# ESP-IDF 5.5.3 only applies WRITE_ENC to generated CCCDs. Wrap the private
# registration boundary so protected CCCDs also receive READ_ENC.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ble_att_svr_register")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,__wrap_ble_att_svr_register")

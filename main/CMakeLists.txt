idf_component_register(
    SRCS "snake_app.c" "snake_art.c" "snake_arcade.c" "snake_bricks.c" "snake_games.c" "snake_model.c"
         "snake_net.c" "snake_fetch.c" "snake_usb.c" "snake_portal.c" "snake_pixels.c" "snake_album.c"
         "snake_setup_dns.c"
         "../assets/fonts/power-snake/snake_font.c"
    EMBED_TXTFILES "snake_diy_portal.html"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_driver_usb_serial_jtag esp_event esp_http_client esp_http_server esp_hw_support esp_netif
             esp_timer esp_wifi json lwip mbedtls nvs_flash
)

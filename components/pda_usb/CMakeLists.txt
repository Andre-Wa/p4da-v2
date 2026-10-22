# pda_usb — USB Host (teclado HID boot protocol).
#
# Herdado 1:1 da v1 (validado em HW): hot-plug real (a task da host lib
# não se mata com o bus vazio), dedup de relatórios, teardown/reinstall
# p/ light sleep, media keys de consumer control (o callback de mídia só
# é registrado a partir da camada L3 — áudio).
#
# A instalação ÚNICA da USB Host Library mora aqui (usb_host_lib_task);
# usb_host_state.h é o flag de "host pronto" que futuros clients (UAC,
# L3) esperam antes de uac_host_install().
idf_component_register(
    SRCS
        "usb_hid_keyboard.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        usb                     # usb/usb_host.h (host library do IDF)
        esp_timer
        espressif__usb_host_hid # usb/hid_host.h
)

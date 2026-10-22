# pda_net — Wi-Fi STA + NTP via coprocessador C6 (ESP-Hosted SDIO).
#
# Camada L2 do p4da-v2, carregada 1:1 do wifi_net.[ch] da v1 (v5.15.0/
# v5.16.x validados em campo): manager de conexão scan-driven com
# multi-redes salvas em config/wifi.lua, rotação por reason de disconnect
# (nomes reais, não "AUTH_FAIL" p/ tudo), backoff exponencial em task
# própria, espera ociosa com re-scan periódico (incidente da 14ª rodada),
# pause/resume de standby e seed de relógio via NVS.
#
# O dono das decisões de rede é docs/WIFI.md (carregado da v1 na próxima
# rodada de docs); este componente NÃO conhece a camada app: o único
# símbolo para cima é wifi_net_on_event_ui() (definido em status_bar.cpp),
# que a v1 já usava assim — candidato a inversão (provider) num refactor
# futuro, como o blind-provider do touch (F1).
idf_component_register(
    SRCS
        "wifi_net.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pda_script          # pda_config.h (timezone/ntp_server/wifi_enabled)
        pda_board           # storage_init.h (pda_path, write de wifi.lua)
        nvs_flash           # esp_wifi + seed/last_utc (ns "pdawifi")
        esp_netif
        esp_event
        lwip                # esp_sntp
        espressif__esp_wifi_remote
    # esp_wifi.h/esp_wifi_types.h vêm do componente esp_wifi (o
    # esp_wifi_remote proxy-iza as chamadas p/ o C6, mas os HEADERS são
    # do esp_wifi — no IDF 5.5 o remote não os re-exporta publicamente).
    # PRIV porque wifi_net.h (header público) não inclui esp_wifi.h:
    # ninguém além deste componente precisa ver esses headers.
    # Achado no 1º build de alvo da v2.1.0 (v2.1.1): na v1 o esp_wifi
    # estava no REQUIRES do main monolítico e se perdeu na migração.
    PRIV_REQUIRES
        esp_wifi
)

# ---------------------------------------------------------------------------
# ABI do espressif/lua 5.5.0 — OBRIGATÓRIO aqui também.
#
# wifi_net.c inclui lua.h para parsear config/wifi.lua em estado Lua
# temporário (padrão da v1: config de rede NÃO passa pela VM dos scripts).
# O componente espressif/lua compila a VM com LUA_32BITS=1 PRIVATE
# (ver o bloco equivalente em components/pda_script/CMakeLists.txt); sem
# repetir o define NESTE componente, o lua_tostring/pcall daqui casariam
# ABI errada com a VM real (lixo/INT_MAX — o mesmo veneno da v1).
# O _Static_assert abaixo (wifi_net.c) falha o build se o define sumir.
# ---------------------------------------------------------------------------
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUA_32BITS=1)

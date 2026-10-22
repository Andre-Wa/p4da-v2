# main — camada de aplicação do p4da-v2.
#
# Dono da UI Slint (ui/app_ui.slint -> app_ui.h gerado) e dos módulos de
# tela (screen_*.cpp) + shell (status bar, sessão, entrada USB, contexto
# compartilhado). Regra de camada: main CONHECE os componentes
# (pda_board/pda_script/pda_power/pda_usb); componentes NÃO conhecem main.
idf_component_register(
    SRCS
        "app_main.cpp"
        "app_context.cpp"
        "status_bar.cpp"
        "session.cpp"
        "screen_launcher.cpp"
        "screen_networks.cpp"
        "screen_files.cpp"
        "screen_editor.cpp"
        "screen_notes.cpp"
        "screen_scripts.cpp"
        "screen_settings.cpp"
        "input_usb.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        pda_board
        pda_script
        pda_power
        pda_usb
        pda_net
        esp_lcd          # tipos de panel/touch p/ slint_esp_init
        nvs_flash        # nvs_flash_init no bring-up
        esp_timer        # relógio da status bar + cursor blink
)

# CMP0116: sem isso o Ninja acha para sempre que o .cpp gerado a partir do
# .slint está desatualizado ("manifest still dirty after 100 tries").
cmake_policy(SET CMP0116 NEW)

# Compila ui/app_ui.slint -> C++ e linka no componente main. O diretório
# do header gerado (app_ui.h) entra no include path deste target — todos
# os screen_*.cpp o incluem.
slint_target_sources(${COMPONENT_LIB} ui/app_ui.slint)

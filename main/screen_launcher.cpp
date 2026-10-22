/**
 * screen_launcher.cpp — despacho dos tiles do launcher.
 *
 * Tiles cinzas (Redes/Música na L1; Agenda/Contatos/Relógio sempre) são o
 * contrato visual do roadmap: o tile existe, mas enabled=false no
 * .slint — o despacho abaixo só conhece os apps da fundação. Os nomes de
 * sessão ("files"/"notes"/...) são o contrato com session.cpp.
 */

#include "screens.h"

#include "esp_log.h"

static const char *TAG = "launcher";

void launcher_wire(AppWindow &ui)
{
    ui.on_open_app_dispatch([](slint::SharedString name) {
        activity();
        std::string n(name.data());
        if (n == "Arquivos") {
            files_open_root();
            session_set_app("files");
        } else if (n == "Notas") {
            notes_open();
            session_set_app("notes");
        } else if (n == "Scripts") {
            scripts_open();
            session_set_app("scripts");
        } else if (n == "Redes") {
            networks_open();
            /* v1: a tela Redes não persiste como sessão própria (o
             * restore não tem ramo p/ ela — volta no launcher). */
            session_set_app("launcher");
        } else if (n == "Config") {
            settings_open();
            session_set_app("settings");
        } else {
            /* Música: camada L3 (docs/ROADMAP.md). Agenda/Contatos/
             * Relógio: L5. Tiles chegam aqui desabilitados; o log é rede
             * de segurança p/ habilitar sem despacho. */
            ESP_LOGW(TAG, "app não disponível nesta camada: %s", n.c_str());
        }
    });
}

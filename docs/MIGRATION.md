# MIGRATION — mapa v1 (v5.16.2) → v2 (L1) e religação das camadas

Referência para duas leituras: (a) "onde foi parar o arquivo X da v1?" e
(b) "o que preciso tocar para religar Wi-Fi/áudio/bateria no v2?". O
repo da v1 permanece como arquivo histórico das validações de campo
(57 commits, uploads/, validacoes/, esquemático).

## 1. Mapa arquivo a arquivo

Legenda: **=** carregado 1:1 · **~** carregado com adaptação ·
**÷** dividido entre módulos · **−** não veio (volta em camada futura).

### BSP → `components/pda_board/`
| v1 | v2 | |
|---|---|---|
| main/board_config.h | include/board_config.h | = |
| main/display_init.[ch] | display_init.c + include/ | = |
| main/touch_init.[ch] | touch_init.c + include/ | ~ (blind-provider, F1) |
| main/storage_init.[ch] | storage_init.c + include/ | = |

### Serviços
| v1 | v2 | |
|---|---|---|
| main/lua_runtime.[ch] | components/pda_script/ | ~ (− pda.battery.*) |
| main/pda_config.[ch] | components/pda_script/ | = |
| main/power_mgmt.[ch] | components/pda_power/ | = |
| main/usb_hid_keyboard.[ch/h] | components/pda_usb/ | = |
| main/wifi_net.[ch] | − | L2 |
| main/audio_uac.[ch], main/audio_i2s.[ch] | − | L3 |
| main/battery.[ch] | − | L4 |

### Camada app (v1 main/main.cpp ÷)
| trecho da v1 (main.cpp) | v2 |
|---|---|
| globals + ui_gate + spawn_thread + log/toast + nav + helpers UTF-8/caminho | main/app_context.{h,cpp} |
| update_clock (relógio/pwr; −batt/−wifi) | main/status_bar.cpp |
| sessão (save/restore, −ramo music) | main/session.cpp |
| launcher dispatch (−Redes/−Musica) | main/screen_launcher.cpp |
| arquivos (lista/ops/prompt/picker) | main/screen_files.cpp |
| editor (cursor/OSK/md/vista) + timer blink | main/screen_editor.cpp |
| notas | main/screen_notes.cpp |
| scripts (lista/run/mutex) | main/screen_scripts.cpp |
| config+quick panel (−wifi/−batt/−som) | main/screen_settings.cpp |
| HID → editor (−media keys cb) | main/input_usb.cpp |
| bring-up + tasks + standby cb | main/app_main.cpp |
| main/ui_gate.h, main/md_render.h | main/ (1:1) |

### UI Slint
| v1 | v2 | |
|---|---|---|
| ui/theme.slint, ui/elements/*, ui/assets/* | main/ui/… | = |
| ui/screens/{editor,files,notes,scripts}.slint | main/ui/screens/ | = |
| ui/app_ui.slint | main/ui/app_ui.slint | ~ (−net/−mus/−batt/−wifi) |
| ui/screens/settings.slint | main/ui/screens/ | ~ (4 abas) |
| ui/elements/quick_panel.slint | main/ui/elements/ | ~ (−rede, 156px) |
| ui/screens/launcher.slint | main/ui/screens/ | ~ (tiles cinzas) |
| ui/screens/networks.slint, music.slint | − | L2/L3 |

### Tools / template / docs
| v1 | v2 | |
|---|---|---|
| tools/hosttest/{run.sh,host_cfg_test.c,host_uigate_test.cpp,lua55_shim.h,stubs/} | tools/hosttest/ | ~ / = |
| tools/hosttest/host_batt_test.c | − | L4 |
| tools/hosttest/host_lua_test.c | tools/hosttest/ | ~ (e2e = logger_exemplo.lua) |
| tools/{md_test.cpp,ppm2png.py,font_pipeline.py,scale_type.py,make_icons.sh,icons.json} | tools/ | = |
| tools/offscreen_render_harness.cpp + render_offscreen.sh | tools/ | ~ (−music/−networks, +launcher, docs/img/) |
| tools/flash_c6_wifi.sh | − | L2 (reflash do C6 p/ Wi-Fi) |
| tools/idfinit_check_i2s.c / idfinit_check_adc.c | − | L3/L4 |
| sdcard-template: config/system.lua, notas/_indice.md, scripts/{exemplo,organiza_notas}.lua | sdcard-template/ | =/~ |
| sdcard-template: config/wifi.lua, music/*.wav, scripts/{log_bateria,ancoras_teste}.lua | − | L2/L3/L4 |
| docs/{HARDWARE,POWER,UI,FONTES,USB}.md | docs/ | = (nota de versão no topo) |
| docs/{LUA,DEPENDENCIAS,ROADMAP}.md | docs/ | reescritos p/ v2 |
| docs/{ANDROID_SYNC,BLUETOOTH}.md | − (ficam na v1) | pesquisa preservada |
| docs/ARCHITECTURE.md | docs/ARCHITECTURE.md | reescrito (v2) |
| — | docs/{BUILD_LOG,MIGRATION,TESTES}.md | novos |

## 2. Religando a L2 (Wi-Fi + NTP via C6)

1. `components/pda_net/` novo: `wifi_net.[ch]` da v1 1:1 (scan-driven
   manager, multi-redes em `config/wifi.lua`, reason por nome, backoff).
   `idf_component.yml`: `espressif/esp_hosted ==2.12.9` +
   `espressif/esp_wifi_remote >=1.3.1` (rules esp32p4); `sdkconfig.defaults`
   ganha as linhas ESP_HOSTED_* + SNTP da v1 (voltam ao build quando o
   componente existir).
2. `main/`: `screen_networks.cpp` novo (wire de `net-*`) + `networks.slint`
   da v1; no `app_ui.slint` devolver: import, `AppState.Networks`, props
   `net-ssids/net-infos/net-locked/net-current`, callbacks `net-pick`/
   `net-rescan`, `status-wifi`, `cfg-wifi-on`+`cfg-set-wifi-on`,
   `cfg-wifi-info`, `cfg-open-networks`; QuickPanel: linha de rede +
   atalho "Redes" e `qs-h`/`target-h` 156→192.
3. `status_bar.cpp`: relógio passa a usar `wifi_net_time_hhmm()` quando
   conectado (uptime é o fallback — exatamente o código da v1);
   `wifi_net_on_event_ui` volta como sink de evento.
4. `app_main.cpp`: `wifi_net_set_enabled_boot()` + `wifi_net_init()` no
   bring-up; pausa/resume no standby cb; `wifi_net_seed_clock()` antes do
   storage (seed de RTC via NVS).
5. Launcher: tile Redes `enabled: true`; dispatch "Redes" + sessão.
6. `tools/flash_c6_wifi.sh` da v1 (host 2.12.9 + slave 2.12.9).

## 3. Religando a L3 (áudio)

1. `components/pda_audio/`: `audio_uac.[ch]` + `audio_i2s.[ch]` da v1 +
   extração do parser WAV p/ `wav_parse.[ch]` (SOM.1 da v1 já planejava);
   yml: `espressif/usb_host_uac ==1.5.0` + `espressif/esp_codec_dev ==1.6.2`.
   O beep (i2s) e o player local (SOM.1) nascem aqui; `pda_usb` registra
   `usb_hid_keyboard_set_media_cb` → player.
2. `main/`: `screen_music.cpp` + `music.slint` da v1; props/callbacks
   `mus-*` de volta no `app_ui.slint`; tile Música ativo; ramo "music" no
   restore de sessão; aba Sistema de Config ganha "Teste som".

## 4. Religando a L4 (bateria)

1. `components/pda_battery/`: `battery.[ch] da v1 1:1` (divisor GPIO53 /
   ADC2_CH4, máquina de estados, âncoras NVS); `tools/hosttest/host_batt_
   test.c` + stubs esp_adc voltam; `tools/idfinit_check_adc.c` volta.
2. `pda_script`: bindings `pda.battery.*` de volta em `lua_runtime.c`
   (diff da v1 preservado neste repo: commit F2 remove, L4 devolve);
   scripts `log_bateria.lua`/`ancoras_teste.lua` voltam ao template.
3. UI: `status-batt` + `cfg-batt-info` + botão "Zerar calibração" (aba
   Sistema) + tick de 4 s em `status_bar.cpp`.

## 5. Compatibilidade de dados

- `system.lua`: formato idêntico v1↔v2 (chaves dormentes incluídas) —
  cartão da v1 boot no v2 sem tocar no arquivo.
- NVS: namespaces/chaves iguais (`pdawifi/last_utc`, sombra de config,
  flag de hibernate, âncoras `batt`) — upgrade/downgrade de firmware
  não perde estado.
- `.state/session.txt`: formato `app=`/`note=` inalterado.
- LittleFS interno: mesma partição/label — espelho sobrevive ao reflash.

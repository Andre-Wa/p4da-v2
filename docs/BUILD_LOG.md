# BUILD_LOG — a construção do p4da-v2 (fase a fase)

Diário da reconstrução "quase do zero" a partir do `p4da-v1` v5.16.2
(commit `7766869`). Cada fase = 1 commit do histórico git (F0–F6); cada
fase fecha com validação de host antes da seguinte (disciplina herdada
da v1: nada avança com pendência da fase anterior).

Fontes de entrada da análise:
- repositório GitHub `Andre-Wa/p4da-v1` (v5.16.1, commit `e4d9889`);
- workspace da v1 (tarball `p4da-v5.16.2-workspace.tar.gz`, sha256
  `b66962c9…`, + mirror git com os 57 commits e docs/validações de campo);
- esquemático da placa e notas de campo citados em `docs/HARDWARE.md`.

## F0 — Esqueleto do projeto

- `CMakeLists.txt` (`project(p4da_v2)`), `partitions.csv` (6M factory +
  8M storage, herdado do M4.7 da v1), `sdkconfig.defaults` = conjunto
  validado na v1 **menos** as linhas de rede (ESP-Hosted/SNTP voltam na
  L2; sem o componente no build as opções não existem).
- Decisão registrada: `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=1024`
  fica desde já (teclado funciona com 256; devices UAC compostos da L3
  morrem em `CHECK_SHORT_CONFIG_DESC` com 256 — achado v1 M5a.1.1).

## F1 — `components/pda_board` (BSP)

Carregados 1:1 da v5.16.2: `board_config.h` (pinagem reconciliada com o
esquemático da JC4880P443C_I_W), `display_init.*` (ST7701S MIPI-DSI +
backlight PWM), `touch_init.*` (GT911 com retry 5x + bus reset do M5.1.1),
`storage_init.*` (SD 4-bit @40 MHz + LittleFS + raiz `/pda` + espelho).

**Única mudança de código da fase (e a mais importante do v2):**
`touch_init.c` chamava `power_mgmt_touch_blind()` — o BSP dependia do
serviço de energia (ciclo board→power que só compilava porque na v1 era
um componente só). v2: `board_touch_set_blind_provider(fn)` (inversão de
dependência; sem provedor = nunca cego = default seguro). O provider é
registrado pelo `app_main` (F4).

## F2 — Componentes de serviço

- `pda_script` = `lua_runtime.c/h` + `pda_config.c/h`. Subtração:
  bindings `pda.battery.*` (read/set/get/reset) removidos — voltam na L4
  com `components/pda_battery`. `pda.version()` → `"p4da 2.0.0"`.
  `LUA_32BITS=1` + comentário de ABI movidos do `main/CMakeLists.txt` da
  v1 para `pda_script/CMakeLists.txt` (único componente que inclui
  `lua.h`); `_Static_assert` de `pda_config.c` segue como cinto.
  `wifi_enabled` mantido no struct/serialização **dormente** (formato de
  `system.lua` idêntico ao da v1 ⇒ `host_cfg_test.c` inalterado).
- `pda_power` = `power_mgmt.c/h` 1:1 (degraus, kill-switch light sleep,
  standby sticky, flag NVS de pós-hibernate).
- `pda_usb` = `usb_hid_keyboard.cpp` + `usb_host_state.h` 1:1 (HID boot
  protocol, hot-plug, dedup; media keys sem callback registrado na L1).

## F3 — UI Slint (subconjunto L1)

Carregados 1:1: `theme.slint` (tokens M3 + 5 paletas), `elements/`
(botões/superfícies/OSK/prompt/ícones), `screens/{editor,files,notes,
scripts}.slint`, assets (PDA Mono 400/700 + MaterialIcons subset).

Adaptados (slint-compiler limpo; API gerada conferida: 77 setters, zero
resíduo `net_*/mus_*/batt/wifi`):
- `app_ui.slint` 534→472 linhas: AppState sem Networks/Music; status bar
  sem batt/wifi; Config sem wifi/bateria/som; QuickPanel sem linha de
  rede (`qs-h` 192→156 — altura é contrato com `target-h` do painel);
- `settings.slint` 239→203: 4 abas (Tela/Sistema/Entrada/Sobre);
- `quick_panel.slint` 161→136: brilho+Config+Suspender;
- `launcher.slint`: tiles Redes/Música `enabled: false` (contrato visual
  do roadmap, mesmo padrão v1 p/ Agenda/Contatos/Relógio).
- `networks.slint`/`music.slint` não vieram (mapa de retorno: MIGRATION).

**Bug encontrado e corrigido no caminho:** na primeira passada apaguei o
5º TabBtn da sidebar de Config (o ícone "info" do Sobre) em vez do 4º (o
wifi): o render offscreen mostrou a aba Sobre com ícone de Wi-Fi. O
render como juiz antes do HW — exatamente para isso que ele existe.

## F4 — Camada de aplicação (o monolito fatiado)

`main.cpp` (2141 linhas) → 11 módulos: `app_main.cpp` (composition
root), `app_context.{h,cpp}` (g_ui, ui_post/gate, spawn_thread, nav,
helpers), `screens.h` (contrato por tela), `screen_{launcher,files,
editor,notes,scripts,settings}.cpp`, `status_bar.cpp`, `session.cpp`,
`input_usb.cpp`. Código transcrito 1:1 onde validado em HW — inclusive
os comentários de campo com datas/rodadas (são a memória das causas
raízes: INT_MAX, assert xTaskToNotify, Guru Meditation UTF-8, loop de
sobrescrita do system.lua...). Cruzamentos legítimos documentados em
`screens.h` (OSK do editor edita o prompt do gerenciador — herança M4.8).

Limpezas sem mudança de comportamento: `g_hid_seen` (set-only desde o
M2), variáveis mortas do `ed_move`, ramo `wifipass` do prompt, caso
Networks do `app_back`, ramo music do restore de sessão.

**Novidade de verificação:** `tools/syntaxcheck/` — typecheck da camada
app no host sem ESP-IDF: `app_ui.h` REAL gerado pelo slint-compiler +
Slint-cpp Linux REAL + headers públicos REAIS dos componentes; stubs só
do sistema (`esp_log/freertos/esp_lcd*/slint-esp`). Resultado: **11/11
TUs OK** com `-Wall -Wextra` (flags alinhadas ao alvo:
`-Wno-missing-field-initializers` já vem nos defaults do IDF).

## F5 — Template do cartão + ferramentas + validação verde

- `sdcard-template/pda/`: `config/system.lua` (comentário de chave
  dormente), `notas/_indice.md`, `scripts/{exemplo,organiza_notas}.lua`
  1:1; **novo** `scripts/logger_exemplo.lua` = o padrão de script longo
  da v1 (log_bateria.lua) sem bateria: sleep por linha, append fechado a
  cada amostra, continuação sem duplicar header, falha graciosa.
- `tools/hosttest/`: `run.sh` re-apontado p/ componentes;
  `host_cfg_test.c`/`host_uigate_test.cpp` 1:1; `host_lua_test.c`
  reescrito sem bateria (A smoke · B mkdir · C sleep · D–F logger
  end-to-end). `tools/md_test.cpp` e pipeline de fontes 1:1.
- `tools/offscreen_render_harness.cpp`: −music/−networks, panel 156 px,
  +modo launcher; renders em `docs/img/` (11 PNGs).

### Resultado da suíte de host (roda em qualquer máquina Linux)

```
hosttest/run.sh:
  cfg    : TUDO PASSOU (0 falha(s))          <- pda_config.c REAL
  uigate : A–G TODOS OS TESTES PASSARAM      <- ui_gate.h REAL (1308 posts concorrentes)
  lua    : A–F OK                            <- lua_runtime.c REAL + logger_exemplo.lua REAL
md_test  : TUDO PASSOU (0 falha(s))          <- md_render.h REAL
luac -p  : 4/4 (3 scripts + system.lua)
syntaxcheck: 11/11 TUs OK
render   : 11 PNGs (launcher c/ cinzas, painel 156px, config 4 abas,
           leitura/edit/prompt/sondas de pitch)
```

## F6 — Documentação + README

README novo; `ARCHITECTURE.md` (este v2), `BUILD_LOG.md` (este),
`MIGRATION.md` (mapa v1→v2 + religação L2–L4), `ROADMAP.md` (camadas),
`TESTES.md` (suíte de host); domínio carregado com nota de versão:
`HARDWARE.md`, `POWER.md`, `UI.md`, `FONTES.md`, `USB.md`; reescritos:
`LUA.md` (sem pda.battery), `DEPENDENCIAS.md` (matriz da fundação).
`ANDROID_SYNC.md`/`BLUETOOTH.md` da v1 ficam no repo da v1 (fora do
escopo L1–L5 atual; pesquisa preservada lá).

## Ambiente de verificação (reproduzível)

```bash
# Debian bookworm: gcc/g++/cmake/lua5.4(+dev)/pillow/qt6/xkb/gbm/libinput
# (Qt6 etc. só p/ linkar o Slint-cpp Linux do render offscreen)
# Slint 1.12.1: slint-compiler (binário) + Slint-cpp (tarball Linux-x86_64)
#   em ~/.tools/ (mesmo layout do env_setup.sh da v1)
SLINT_COMPILER=$HOME/.tools/slint-compiler \
SLINT_CPP=$HOME/.tools/Slint-cpp-1.12.1-Linux-x86_64 \
  bash tools/render_offscreen.sh
```

## O que NÃO foi validado (e como será)

Nada do v2 rodou ainda no hardware — a v1 validou os mesmos módulos
carregados (BSP/energia/Lua/UI), mas a integração nova (bring-up do
app_main, injeção do blind-provider, divisão de módulos) precisa de uma
rodada de placa. Checklist sugerido da 1ª rodada (espelha a disciplina
das validações da v1): boot com/sem cartão · standby por idle e manual
(acorda por botão) · hibernar na Config e restaurar nota · editar nota
pelo OSK e pelo teclado USB · rodar exemplo.lua e logger_exemplo.lua ·
arquivos: renomear/copiar/mover/apagar · pulldown de brilho · regressões
de toque (gesto de voltar, opt-out do editor).

---

# v2.1.0 — camada L2 (Wi-Fi + NTP via C6)

## F7 — `components/pda_net` + UI de redes

Contexto: o usuário validou a L1 em hardware (boot, config, leitura/
edição de arquivos, scripts) e publicou o repo em
`github.com/Andre-Wa/p4da-v2`. Próximo passo do roadmap = L2.

- `components/pda_net/` = `wifi_net.[ch]` da v1 **1:1** (manager
  scan-driven v5.15.0: multi-redes, rotação por reason com nomes reais,
  backoff em task própria, espera ociosa com re-scan 120 s + relax 10
  min, pause/resume de standby, seed de relógio NVS). Únicos acréscimos:
  `_Static_assert(sizeof(lua_Number)==4)` (o componente inclui `lua.h`
  p/ parsear `config/wifi.lua` em estado temporário → herda o contrato
  `LUA_32BITS=1`, agora em DOIS CMakeLists) e o yml
  (`esp_hosted ==2.12.9` + `esp_wifi_remote >=1.3.1`, rules esp32p4).
- `sdkconfig.defaults`: voltam as linhas ESP_HOSTED_* (heap interno no
  boot — assert app_startup.c:86 da v1) e SNTP.
- UI: `networks.slint` (v1 1:1) + props/callbacks `net-*`,
  `status-wifi`, `cfg-wifi-on/info`, `cfg-open-networks` de volta no
  `app_ui.slint`; QuickPanel 156→192 px com linha de rede + atalho;
  Config com 5 abas (Redes volta); tile Redes ativo no launcher.
- App: `screen_networks.cpp` novo (scan/pick/prompt de senha via prompt
  global do gerenciador — `g_wifi_pick_ssid` em `screens.h`); ramo
  `wifipass` de volta em `screen_files.cpp`; chave de rádio +
  open-networks em `screen_settings.cpp`; `wifi_net_on_event_ui` +
  relógio HH:MM em `status_bar.cpp`; seed/pause/resume/init em
  `app_main.cpp`; dispatch "Redes" no launcher.
- `tools/flash_c6_wifi.sh` (v1) + `sdcard-template/pda/config/wifi.lua`
  (formato legado aceito pelo loader; o firmware reescreve como lista).
- Docs: `WIFI.md` carregado (dono das decisões de rede); ARCHITECTURE
  §4.6 (o único upcall do sistema), grafo com pda_net; ROADMAP L2
  código-pronto; MIGRATION §2 marcado executado; DEPENDENCIAS/TESTES/
  README atualizados.

Validação de host desta fase: syntaxcheck **12/12 TUs** · hosttest cfg/
uigate/lua-bind verdes (inalterados) · md 0 falhas · slint limpo ·
**12 renders** (networks de volta, panel 192 px). Wi-Fi em si NÃO tem
teste de host (pilha wifi_remote não stubável com custo razoável — a v1
também não tinha): aceite em hardware via
`release/v2.1.0/CHECKLIST_HW_v2.1.0.md`.

---

# v2.1.1 — hotfix de build (esp_wifi em pda_net)

## F9 — `PRIV_REQUIRES esp_wifi` no pda_net

**Sintoma (1º build de alvo da v2.1.0, máquina do usuário):**
`fatal error: esp_wifi.h: No such file or directory` em
`components/pda_net/wifi_net.c`. O próprio IDF diagnostica: o header é do
componente `esp_wifi`, ausente dos requirements de pda_net.

**Causa raiz:** na v1 o `esp_wifi` estava no REQUIRES do `main`
monolítico (que continha wifi_net.c); na migração p/ componentes o
`esp_wifi_remote` entrou no yml mas o `esp_wifi` (dono dos HEADERS — o
remote proxy-iza chamadas, não re-exporta headers no IDF 5.5) se perdeu.
Build de host não pega isso: o syntaxcheck cobre `main/*.cpp` contra
stubs, e os `.c` de componentes só compilam no build de alvo.

**Fix:** `PRIV_REQUIRES esp_wifi` (PRIV porque `wifi_net.h`, header
público, não inclui `esp_wifi.h` — ninguém além do componente precisa).

**Auditoria completa de includes × REQUIRES (todos os componentes):**
feita à mão contra o IDF 5.5 — pda_board (20 includes: esp_lcd,
esp_driver_gpio/i2c/ledc, fatfs, sdmmc, 4 gerenciados, comuns), pda_script
(lua via espressif__lua, nvs_flash, esp_timer, pda_board), pda_power
(esp_driver_gpio, esp_sleep/esp_system comuns, nvs_flash), pda_usb (usb +
hid_host), pda_net (esp_event, esp_netif, lwip/sntp, nvs_flash, lua via
cadeia pública pda_script→espressif__lua, + esp_wifi PRIV). **Único gap
era o esp_wifi.** Tabela fica como referência p/ componentes futuros:
todo include de componente tem que casar com REQUIRES/PRIV_REQUIRES ou
com a lista comum do IDF (cxx/newlib/freertos/esp_hw_support/heap/log/
soc/hal/esp_rom/esp_common/esp_system).

**Mudança de processo (registra aqui):** release só taggeia depois de UM
build de alvo completo (`idf.py build`) — host verde é necessário mas não
suficiente; visibilidade de include entre componentes é propriedade do
build de alvo. Os warnings de `esp_lcd_touch_get_coordinates` deprecated
no slint-esp.cpp seguem ACEITOS (docs/DEPENDENCIAS.md, warnings aceitos).

---

# Rodada de validação em hardware — v2.0.0/v2.1.1 (2026-10-23)

Usuário (dono da placa) rodou o CHECKLIST_HW_v2.1.0 + v2.1.1 e reportou:

- **L1 (Parte A): FECHADA.** A2–A4 com serial (standby manual sticky:
  `ACTIVE -> DIM (pedido manual)` -> `STANDBY` em ~200 ms, wake por BOOT;
  hibernate com `sessão salva: app=noteedit note=notes/Exemplo.md` e
  restore no boot pós-reset via `ui-gate: 1 evento(s)`), A5–A10 (OSK ×
  USB, markdown, ops de arquivo, pulldown, gestos), A11 (logger CSV +
  serialização de scripts), A12 (persistência). HWM de standby saudáveis
  (ui_loop 23,2–23,7 KB livres).
- **L2 (Parte B): FECHADA exceto B10.** B1 (offline sem wifi.lua), B2–B9
  (conexão boot, NTP+TZ, scan/prompt/senha salva, chave de rádio, quick
  panel, reason por nome, wifi.lua reescrito como lista), B11
  (regressões). Pause/resume de standby com NTP vivo no wake
  (`NTP pausado`/`NTP retomado` — regressão M4.14.2 segue curada).
- **B10 (seed de relógio): procedimento entregue** com a resposta ao
  reporte (3 passos: semear com NTP, cortar rádio, reboot observando
  status bar + log `relógio semeado do NVS`). Aberto até o reporte.

Com isso o ROADMAP marca L1 e L2 como **aceitas em hardware** (B10
pendente isolado, sem bloquear L3: é caminho de boot independente).

## F12 — Errata do B10 (seed de relógio) + comentário de projeto

Reporte do usuário (2026-10-23): a linha `relógio semeado do NVS` aparece
no boot sem rede, mas a status bar segue `00:00:00`. Investigação: o seed
(wifi_net_seed_clock → settimeofday) de fato roda; quem gateia a status
bar em HH:MM é `s_synced` (NTP) — na v1 e na v2. O seed existe para
`time()`/mtimes/`os.date()` (coluna "hora" dos loggers offline), não para
a tela: sem RTC, o relógio semeado deriva desligado e exibi-lo como hora
real seria enganoso. **O erro era do checklist B10**, não do firmware.
Correções: texto do B10 no CHECKLIST_HW_v2.1.0.md (teste via `os.date()`
no Lua com rádio off), ROADMAP (B10 fechado com errata), comentário de
projeto em status_bar.cpp. Sem mudança de comportamento → sem nova
versão de firmware (rebuild opcional, só comentário).
Obs. de rodada: o `Corrupted dir pair ... formatting...` do LittleFS após
`erase-flash` é o caminho documentado (partição storage apagada → mount
formata; espelho interno zera, SD/NVS intocados — nota M4.7 da v1).

## F13 — B10 fechado (evidência do usuário)

Com rádio desligado pós-seed: `pda.log("B10:", os.date(...), os.time())`
→ `B10: 2026-10-24 21:00:05 1792886405`. Os dois campos são o MESMO
instante (epoch é UTC; os.date aplica TZ America/Sao_Paulo: 1792886405 =
2026-10-25 00:00:05Z = 2026-10-24 21:00:05 BRT). Data plausível sem rede
= seed do NVS funcionando. B10 marcado [x] no checklist e no ROADMAP;
L1 e L2 agora estão 100% aceitas em hardware.

---

# v2.2.0 — camada L3 (áudio: falante local + UAC + rota)

## F14 — `components/pda_audio` + tela Música

- `wav_parse.[ch]` NOVO: parser PCM16 extraído do audio_uac.c da v1 para
  servir aos DOIS backends e ao host. Teste novo `host_wav_test.c` com
  WAVs sintéticos — e ele já pagou o preço: o caso C (LIST de tamanho
  ímpar) falhou no primeiro run porque o WRITER do teste não emitia o
  pad de word alignment que o RIFF exige e o parser presume; corrigido o
  writer (o parser estava certo).
- `audio_i2s.[ch]`: beep da v1 (mutex 50 ms, envelope, log no início) +
  player local SOM.1 (task spk_play 8K PSRAM, 44k1/48k, mono→estéreo,
  pause fecha o codec, stop por flag, recusa honesta de outras taxas) +
  handshake beep×música (SOM.1 spec do AUDIO.md: beep suspende o player
  entre chunks, ≤ ~23 ms, player reabre na taxa dele) + volume/mute
  SOM.3 no codec.
- `audio_uac.[ch]`: v1 1:1 (pause/resume com posição, retry de write
  pause-aware, resume pós-unplug, volume/mute em task descartável,
  quirks H3S documentados) — só o parse_wav static virou wav_parse.
- `audio_route.[ch]` (SOM.2): auto/interno/USB, RAM-only (persistência
  = SOM.6), resolvido no play de cada faixa.
- App: `screen_music.cpp` (lista, dev/state/track do backend DONO da
  faixa, rota na tela), media keys despachadas p/ `music_on_media_key`
  (input_usb registra o cb que a L1 deixara solto), sessão "music",
  dispatch "Musica", Config>Sistema "Teste som (falante)".
- UI: music.slint + botões de rota; app_ui.slint mus-* + route +
  cfg-test-speaker + AppState.Music; tile Música ativo; render music.
- yml/CMake: esp_codec_dev 1.6.2 + usb_host_uac 1.5.0 (UAC1-only,
  DEPENDENCIAS #3); REQUIRES pda_board/pda_usb/usb/esp_driver_i2s.

Validação: wav A–F OK · syntaxcheck 13/13 · cfg/uigate/lua-bind/md
verdes · slint limpo · 13 renders (music incl.). Áudio em si = aceite
de HW (checklist da release): é o subsistema com mais quirks de campo da
v1 — NADA dele é stubável com confiança no host.

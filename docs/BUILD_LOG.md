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

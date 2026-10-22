# Arquitetura do p4da-v2 (L1 fundação)

Este documento é o DONO das decisões de arquitetura. Mudar camada,
fronteira ou regra de threading sem atualizar aqui (e sem medição, quando
a regra nasceu de um incidente de campo) é regressão.

## 1. Visão em camadas

```
┌───────────────────────────────────────────────────────────────────┐
│ UI (Slint 1.12.1)                    main/ui/app_ui.slint         │
│  status bar · launcher · arquivos · notas/editor · scripts · cfg  │
│  theme M3 + elements (botões/OSK/prompt/painel/ícones)            │
├───────────────────────────────────────────────────────────────────┤
│ Camada de aplicação (main/, C++)                                  │
│  app_main (composition root) · screen_* (1 tela = 1 módulo)       │
│  app_context (ui_post/threads/nav) · session · status_bar ·       │
│  input_usb                                                        │
├───────────────┬───────────────┬───────────────┬───────────────────┤
│ pda_script    │ pda_power     │ pda_usb       │ (futuros:         │
│ VM Lua 5.5 +  │ degraus de    │ USB Host HID  │  pda_net L2,      │
│ config Lua    │ energia       │               │  pda_audio L3,    │
│ (LUA_32BITS)  │               │               │  pda_battery L4)  │
├───────────────┴───────────────┴───────────────┴───────────────────┤
│ pda_board — BSP da JC4880P443C_I_W                                │
│  board_config.h (pinagem reconciliada) · display ST7701S ·        │
│  touch GT911 · storage SD+LittleFS+raiz /pda                      │
└───────────────────────────────────────────────────────────────────┘
```

**Direção da dependência é para baixo, sempre.** Componentes não incluem
nada da camada app; a camada app não inclui nada de outra camada app
(exceto via `screens.h`/`app_context.h`, o contrato interno). Grafo real:

```
pda_board  ←  pda_script  ←  pda_power
pda_usb (independente)
main → {pda_board, pda_script, pda_power, pda_usb}
```

### 1.1 Por que componentes ESP-IDF (decisão de arquitetura do v2)

Na v1 tudo vivia em `main/` (um componente só, 12 .c/.cpp + o monolito
`main.cpp` de 2141 linhas). Funcionava, mas três coisas doíam:

1. **Nada impedia acoplamento.** O BSP chamava o serviço de energia
   (`touch_init.c` → `power_mgmt_touch_blind()`): ciclo board→power que
   só não virou bagunça porque era um componente só.
2. **ABI de Lua era um landmine global.** `LUA_32BITS=1` precisava estar
   no CMake de `main` inteiro porque `lua_runtime.c` morava lá — o
   comentário de 30 linhas no CMakeLists era o único guarda-corpo.
3. **Evoluir = editar o monolito.** Cada milestone mexia nas mesmas 2141
   linhas; diffs grandes, conflitos de contexto, regressões de escopo.

O v2 corrige os três: fronteiras de componente tornam o ciclo board→power
*impossível de compilar* (resolvido com inversão de dependência, §4.2);
`LUA_32BITS=1` mora no único componente que inclui `lua.h`
(`pda_script/CMakeLists.txt`, com o `_Static_assert` de `pda_config.c`
como cinto); e cada tela/serviço novo é um arquivo ou componente novo.

O que **não** virou componente: a UI Slint e os módulos de tela. O
`.slint` gera um único `app_ui.h` consumido por todos os `screen_*.cpp`
(mesmo target CMake), e o design system (theme/elements) é um só corpo —
fatiar em componentes só acrescentaria fricção de include sem ganhar
fronteira real. A fronteira que importa (app ↔ serviços ↔ BSP) existe.

## 2. Modelo de armazenamento (herdado da v1, intacto)

- Flash 16 MB (`partitions.csv`): NVS 24 KB @ 0x9000 · phy 4 KB ·
  **`factory` (app) 6 MB @ 0x10000** · **`storage` (LittleFS) 8 MB**
  (offset automático 0x610000). ~1,9 MB livres no fim (reserva OTA/assets).
- `/internal` — LittleFS (partição `storage`). Sempre montado.
- `/sdcard` — FatFS via SDMMC 4-bit @ 40 MHz (fallback 20 MHz/400 kHz).
- **Raiz lógica**: `pda_root()` = `/sdcard/pda` com cartão,
  `/internal/pda` sem. Toda camada de cima usa `pda_path()` — nunca
  mount hardcoded. Árvore semeada no boot:
  `config/ scripts/ notes/ music/ logs/ .state/`.
- **Espelho de settings**: `pda_config_save()` grava na raiz ativa e, em
  melhor esforço, na outra; primeiro boot com cartão novo promove o
  interno. Sem hot-plug: cartão lido no boot (remontagem a quente segue
  no backlog, só o caminho de wake do light sleep usa `storage_remount_sd`).
- O subtype da partição `storage` aparece como `spiffs` (0x82) mas o
  conteúdo é LittleFS: mount por LABEL; renomear o rótulo exigiria reflash
  com perda do espelho — decisão v1 mantida e documentada aqui.

## 3. Regras de threading (não negociáveis)

1. **UI nunca bloqueia**: toda I/O de disco roda em `spawn_thread()`
   (FreeRTOS task com pilha em PSRAM, auto-delete) e o resultado volta
   via `ui_post()`. Nomes de task são contrato (`io_edit`, `io_list`,
   `io_notes`, `io_scripts`, `lua_script`, `ui_loop`, `ui_gate` — o
   log de HWM do pda_power casa por nome).
2. **UI-LOOP-GATE**: `ui_post()` é o único caminho para
   `slint::invoke_from_event_loop` fora do event loop. Antes do loop
   existir, o gate ACUMULA (FIFO); a task `ui_gate` dá flush 100 ms após
   a `ui_loop` subir. Causa raiz (v5.16.0/v1, 15ª rodada): no backend ESP
   do Slint 1.12.1 a task estática é NULL até a 1ª linha de
   `run_event_loop`, e `invoke` faz `vTaskNotifyGiveFromISR(task,NULL)`
   incondicional → `configASSERT(xTaskToNotify)` no tasks.c do IDF 5.5.1.
   Qualquer callback de driver durante o bring-up cai nesse buraco.
   Evidência/teste: `main/ui_gate.h` + `tools/hosttest/host_uigate_test.cpp`.
3. **VM Lua é single-thread**: execuções serializadas por `g_lua_mtx`
   (dono: `screen_scripts.cpp`); scripts rodam na task `lua_script`
   (32 K em PSRAM) para não travar a UI. `pda.sleep_ms()` suspende só
   essa task e NÃO gasta o teto de 20M de instruções (o hook conta
   instruções, não tempo) — loggers de minuto rodam horas.
4. **Mutação de propriedades Slint só no event loop** (regras 1 e 2 são
   o mecanismo).
5. `power_mgmt_activity()` em todo handler de UI/teclado/toque — via
   `activity()`; é o que segura os degraus de energia.
6. **Callback de driver NUNCA invoca direto na UI** (HID/Lua/storage):
   sempre `ui_post()`.

## 4. Contratos entre camadas

### 4.1 Configurações (pda_script)

Struct C `pda_settings_t` em RAM é a fonte da verdade; `config/system.lua`
é a serialização legível (chunk que retorna tabela). Parse em **estado Lua
temporário separado** (script quebrado não corrompe settings) + sanitize
com clamp de sanidade + sombra NVS (cura espelho→NVS→defaults).
`pda_settings_set/get` por chave canônica (`display.brightness`,
`power.dim_after_s`, `ui.cursor`, ...) usada pela tela Config e pela API
Lua. Chaves de camadas futuras (`net.wifi_enabled`) permanecem no struct
e no arquivo **dormentes** de propósito: o formato de `system.lua` é
idêntico ao da v1 e o host_cfg_test não muda entre camadas.

### 4.2 BSP ↔ energia: inversão de dependência (decisão do v2)

O proxy de "toque cego" do standby (M4.14.3/M4.15 da v1: o Slint polla o
driver de touch por conta própria; com a tela off o GT911 entregava
pontos e elementos invisíveis disparavam actions fantasmas) fazia o BSP
chamar `power_mgmt_touch_blind()`. No v2 o BSP expõe
`board_touch_set_blind_provider(fn)` e o **composition root**
(`app_main.cpp`) registra `power_mgmt_touch_blind` como provedor. Sem
provedor o touch nunca é cego (= comportamento ACTIVE, seguro por
default). Padrão para as próximas injeções (ex.: L2 vai registrar
pausa/retomada de rede no standby sem o BSP saber que rede existe).

### 4.3 USB Host (pda_usb)

A instalação ÚNICA da Host Library mora em `usb_hid_keyboard.cpp`
(`usb_host_lib_task`). `usb_host_state_ready()` é o flag que clients
futuros (UAC na L3) esperam antes de `uac_host_install()` — na v1 sem
isso o 2º client falhava o registro com uma linha E fixa em todo boot.
Media keys de consumer control existem no driver; o callback só é
registrado a partir da L3 (na L1 teclas de mídia são ignoradas).

### 4.4 UI ↔ C++ (contrato Slint)

`app_ui.slint` é a dona da API pública (`ed-*`, `file-*`, `fm-*`,
`cfg-*`, `qs-*`, `status-*`): propriedades `in/in-out` + callbacks.
Telas em `screens/*.slint`, tokens em `theme.slint`, peças em
`elements/*.slint`. Dois contratos de métrica: `ED_LINE_H=28px` e
`ED_CHAR_W=11px` espelhados no C++ (fonte mono) — o harness
`fontpitch` mede o avanço real do renderer e pega drift. Charset: a
âncora de rasterização (literal estático Latin-1+tipográficos no
`app_ui.slint`) é o que faz acentos vindos do C++ renderizarem — não
remover sem reler o incidente da crase (M4.10).

### 4.5 Sessão de hibernação (session.cpp)

`.state/session.txt` = `app=<nome>` + `note=<título>` (note só para
`noteedit`). Boot pós-hibernate é sempre RESET (deep sleep do P4 não tem
wake source de usuário — botões fora do domínio LP): o flag NVS do
pda_power diz que há sessão; o restore **parseia** no bring-up e
**aplica** no event loop via `ui_post` (M4.15.3: aplicar antes do loop
era o mesmo assert do §3.2).

## 5. Proteção da VM Lua (herdada, intacta)

- `lua_pcall` sempre → erro de script vira mensagem no console, não panic.
- Hook de contagem: `PDA_LUA_INSTRUCTION_LIMIT` (20M) mata loop infinito.
- Allocator próprio com teto `PDA_LUA_MEM_LIMIT_BYTES` (2 MB).
- Caminhos: relativos à raiz ativa ou `/sdcard|/internal`; `..` rejeitado.
- **ABI 32-bit do espressif/lua 5.5** (o define é PRIVATE no componente
  deles): `pda_script/CMakeLists.txt` repete `LUA_32BITS=1` e
  `pda_config.c` tem `_Static_assert(sizeof(lua_Number)==4)`. Causa raiz
  do "veneno INT_MAX" da v1 (boots 24–30/09): ler float como double satura
  em INT_MAX no RISC-V.

## 6. Energia em degraus (resumo — o dono é docs/POWER.md)

ACTIVE → DIM (backlight ~12%) → STANDBY (tela off; light sleep atrás do
kill-switch `power_mgmt_light_sleep_active()` = false até o rework
M-power: no wake, o remount do SDMMC compartilhado com o SDIO do hosted
causava Instruction access fault) → HIBERNATE (deep sleep manual ou por
idle; volta = power-on com sessão restaurada). Standby manual sticky
(M4.13): o pedido sobrevive à cauda de eventos de toque do próprio tap.

## 7. O que deliberadamente NÃO está na L1

Wi-Fi/NTP/BT (L2), áudio UAC/I2S/player (L3), bateria/`pda.battery.*`
(L4), apps Agenda/Contatos/Relógio e OTA (L5). Tiles cinzas no launcher =
contrato visual do roadmap. Os pontos exatos de religação (propriedades
Slint, callbacks, yml de dependências, sdcard-template) estão em
`docs/MIGRATION.md`.

## 8. Princípios herdados da v1 (NÃO mexer sem medição)

- **LittleFS** (não SPIFFS): fail-safe + wear leveling + dirs.
- **Slint 1.12.1 pinado** (bloat de glifos no 1.18 — 31,8 MiB de .rodata).
- **IDF 5.5.x pinado** (6.x recusa P4 rev <3.1; série 2.x do st7701 muda API).
- **Standby robusto + kill-switch do light sleep** até o rework M-power.
- **Sombra NVS + censo/FNV + self-test do parser** de config.
- **Transições de estado assinadas** (`pwr: X -> Y`) no log.
- **Render offscreen como juiz antes do HW** (achou bugs que 3 rounds de
  conjectura não acharam) — agora com `tools/syntaxcheck` ao lado.

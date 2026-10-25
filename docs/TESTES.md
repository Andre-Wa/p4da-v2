# TESTES — a suíte de host do p4da-v2 (sem hardware)

Disciplina herdada da v1 (REGRESSOES_BUILD): lógica pura nasce com teste
de host; UI nova nasce com render offscreen; só então vai à placa. O v2
acrescentou o syntaxcheck da camada app. Tudo roda em Linux x86_64 com
gcc/g++, lua5.4 (+dev) e, p/ o render, Slint-cpp 1.12.1 + Qt6/xkb/gbm.

## 1. `tools/hosttest/run.sh` — código REAL contra stubs de sistema

| seção | código real sob teste | o que prova |
|---|---|---|
| cfg | `components/pda_script/pda_config.c` | parse/serialize/sanitize de `system.lua`, censo/FNV, sombra NVS, clamp de sanidade, ordem das seções (a v1 quebrou aqui: INT_MAX, mídia insana) |
| uigate | `main/ui_gate.h` | UI-LOOP-GATE: acumular antes do loop, flush FIFO, idempotência, concorrência sem perda (1308 posts), post aninhado, cenário do crash da 15ª rodada |
| wav | `components/pda_audio/wav_parse.c` | parser SOM.1 contra WAVs sintéticos: canônico off/len, fmt estendido, LIST ímpar+FACT (pad de word), não-PCM rejeitado, RIFF≠WAVE/fmt truncado, mono 22050 (recusa de taxa é do player) |
| lua-bind | `components/pda_script/lua_runtime.c` + `sdcard-template/pda/scripts/logger_exemplo.lua` | VM protegida no Lua 5.4 (shim p/ lua_newstate de 3 args), bindings fs/mkdir/sleep, e o script REAL end-to-end: CSV criado (header+8 linhas crescentes), continuação sem duplicar header, falha graciosa sem diretório |

Stubs em `tools/hosttest/stubs/`: só o sistema (esp_log/esp_err/esp_timer/
esp_random/freertos/nvs/storage fake com stdio real em /tmp). O que o
teste afirma é comportamento do NOSSO código, não do stub.

## 2. `tools/syntaxcheck/run.sh` — typecheck da camada app (novo no v2)

Gera `app_ui.h` com o **slint-compiler 1.12.1 real** a partir do
`app_ui.slint` real e compila `main/*.cpp` com `-fsyntax-only -Wall
-Wextra` contra: Slint-cpp Linux real (headers), headers públicos reais
dos componentes, `md_render.h`/`ui_gate.h`; stubs apenas de sistema
(`tools/syntaxcheck/stubs/`, incluindo `slint-esp.h` de superfície).
Pega drift de API Slint (setter removido/renomeado) e erros de tipo da
divisão de módulos — sem esperar o build do alvo. 11/11 TUs.

## 3. `tools/md_test.cpp` — parser markdown

`md_render.h` (o MESMO header do firmware e do harness): estilos de
bloco (h1/h2/h3, bullet, citação, cerca, hr, parágrafo), inline
(**negrito**, `código`), tolerâncias (#sem espaço, BOM, CRLF), regressões
históricas (título invisível por line-box, crase sozinha).

## 4. `tools/render_offscreen.sh` — UI como juiz antes do HW

Compila o `.slint` em modo `embed-for-software-renderer` e renderiza 11
modos em `docs/img/`: reading/reading2 (markdown + scroll), edit
(cursor under/block), networks (lista scan-driven), music (lista+rota+estado), prompt (OSK + cursor
de campo), panel/paneldrag (pulldown aberto/arrastado), settings/
settings2 (abas), launcher (tiles do roadmap), coltest/fontpitch (sondas de pitch do glifo mono —
drift do cursor-bloco). Foi assim que a v1 achou o bug do prompt que 3
rodadas de conjectura não acharam — e foi assim que o v2 achou o ícone
errado da sidebar de Config (F3→F5).

## 5. Checagens rápidas (disciplina de commit)

- `lua5.4 -e loadfile(...)` p/ cada .lua do template (sintaxe);
- `slint-compiler --format cpp` limpo no `app_ui.slint` (sintaxe UI);
- paridade de chaves/parênteses nos .c/.cpp editados à mão (herança v1).

## O que a suíte NÃO cobre (e precisa de placa)

Bring-up real (sequência de init, PSRAM, DSI), timings de energia,
comportamento de SD/host USB físico, GT911 warm-boot, áudio e TODO o
caminho Wi-Fi/NTP (pda_net não tem teste de host — aceite em hardware,
checklist em release/v2.1.0/CHECKLIST_HW_v2.1.0.md). Por isso
cada camada fecha com rodada de hardware + checklist em
`docs/BUILD_LOG.md`/`docs/ROADMAP.md` — e os uploads de campo
(logs/CSVs) continuam sendo anexados ao histórico como evidência.

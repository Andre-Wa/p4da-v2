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
| audio | `audio_tags.c`, `audio_playlist.c`, `audio_decode.c` | tags ID3v2.4 do mp3 REAL do template + LIST/INFO sintético + sem-tag; m3u (comentários/#EXT/relativos/absolutos/skips); pcm_src WAV exato e **MP3 real decodificado no host** (~88k frames/2 s); recusas (22050, lixo) |
| wav | `components/pda_audio/wav_parse.c` | parser SOM.1 contra WAVs sintéticos: canônico off/len, fmt estendido, LIST ímpar+FACT (pad de word), não-PCM rejeitado, RIFF≠WAVE/fmt truncado, mono 22050 (recusa de taxa é do player) |
| lua-bind | `components/pda_script/lua_runtime.c` + `sdcard-template/pda/scripts/logger_exemplo.lua` | VM protegida no Lua 5.4 (shim p/ lua_newstate de 3 args), bindings fs/mkdir/sleep, e o script REAL end-to-end: CSV criado (header+8 linhas crescentes), continuação sem duplicar header, falha graciosa sem diretório |
| msg-budget | `audio_i2s.c`, `audio_uac.c` (análise estática, `tools/hosttest/msg_budget.py`) | bytes UTF-8 de cada literal passado a `set_state()` ≤ `s_state[N]` declarado. Existe porque a v2.3.0 morreu no alvo em `-Werror=format-truncation` (mensagem de 53 bytes em buffer de 48) e o host não via: `-Wformat-truncation` só dispara com inlining, que o GCC 12 daqui nem sempre faz (e o GCC 14 do alvo faz) |

Stubs em `tools/hosttest/stubs/`: só o sistema (esp_log/esp_err/esp_timer/
esp_random/freertos/nvs/storage fake com stdio real em /tmp). O que o
teste afirma é comportamento do NOSSO código, não do stub.

## 2. `tools/syntaxcheck/run.sh` — typecheck em duas camadas

**Estágio 1 — app (`main/*.cpp`).** Gera `app_ui.h` com o
**slint-compiler 1.12.1 real** a partir do `app_ui.slint` real e compila
os 13 TUs contra: Slint-cpp Linux real (headers), headers públicos reais
dos componentes, `md_render.h`/`ui_gate.h`; stubs apenas de sistema
(`tools/syntaxcheck/stubs/`, incluindo `slint-esp.h` de superfície).
Pega drift de API Slint (setter removido/renomeado) e erros de tipo da
divisão de módulos — sem esperar o build do alvo.

Desde a v2.3.1 os TUs que contêm `snprintf` vão com `-O2 -c` (não só
`-fsyntax-only`) e `-Werror=format/-Werror=format-truncation/
-Werror=format-overflow`: esses diagnósticos só existem com otimização.
TUs sem `snprintf` seguem em `-fsyntax-only` — a escolha é automática
(`grep`), não uma lista que apodrece; `app_main.cpp` (composition root,
templates Slint pesados) cai nesse caso e ainda por cima é morto por
falta de memória no sandbox já no `-O1`.

**Estágio 2 — componentes vs headers REAIS.** Nove fontes
(`audio_uac.c`, `audio_i2s.c`, `audio_vol.c`, `audio_decode.c`,
`audio_tags.c`, `audio_playlist.c`, `audio_route.c`, `wav_parse.c`,
`usb_hid_keyboard.cpp`) compilam `-O2` contra `tools/syntaxcheck/real/usb/`
(headers públicos do IDF 5.5.1 baixados do repositório oficial + o
`uac_host.h` real do componente 1.5.0) e contra
`tools/syntaxcheck/stubs-i2s/` (I2S/esp_codec_dev — assinaturas derivadas
do uso; ver o README dali). Nasceu na v2.2.2, quando o watcher USB da
v2.2.1 passou no syntaxcheck com **API inventada** e só quebrou no alvo.

Desde a v2.3.1 o estágio replica a **severidade do GCC 14** do IDF 5.5:
`-Werror=incompatible-pointer-types`, `-Werror=int-conversion`,
`-Werror=implicit-function-declaration`, `-Werror=implicit-int`,
`-Werror=return-type` + as de formato. Motivo concreto: a v2.3.0 passou
aqui e morreu no alvo porque o GCC 14 promove ponteiro incompatível a
erro por padrão e o GCC 12 do sandbox só avisa
(`uac_host_device_write()` recebe `uint8_t*`, o `pcm_src` entrega
`int16_t*`). Warnings não reprovam, mas agora **aparecem** (o alvo
mostrou `unused variable` junto com o erro — ruído que eu quero ver).

### Limites conhecidos (o que isto NÃO prova)

- **Contrato de API stubada.** `stubs-i2s/` e `stubs/usb/hid_host.h` são
  derivados do uso: pegam erro de forma (sintaxe, tipo, formato, buffer),
  não drift do componente real. A autoridade continua sendo o
  `idf.py build`.
- **Inlining ≠ alvo.** O `-Wformat-truncation` do host depende de o GCC
  12 inlinar como o 14 inlina — por isso a seção `msg-budget` existe como
  garantia determinística para `s_state`.
- **Sem link.** `-c -o /dev/null`: símbolo inexistente entre TUs só
  aparece no alvo.

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
comportamento de SD/host USB físico, GT911 warm-boot, o caminho de áudio
no hardware (I2S/ES8311 real, UAC no fone) e TODO o caminho Wi-Fi/NTP
(pda_net não tem teste de host — aceite em hardware, checklist em
release/v2.1.0/CHECKLIST_HW_v2.1.0.md). A *forma* do código de áudio é
checada no host (estágio 2 + msg-budget desde a v2.3.1); o que sobra é
contrato de API stubada e comportamento elétrico/temporal. Por isso
cada camada fecha com rodada de hardware + checklist em
`docs/BUILD_LOG.md`/`docs/ROADMAP.md` — e os uploads de campo
(logs/CSVs) continuam sendo anexados ao histórico como evidência.

Regra prática herdada da v2.1.0/v2.2.1/v2.3.0 (as três não compilaram de
primeira no alvo): **release só é considerada entregue depois de UM
`idf.py build` completo do usuário**, e o checklist da release seguinte
começa por "o build passou limpo".

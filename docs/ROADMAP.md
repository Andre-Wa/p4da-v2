# ROADMAP do p4da-v2 — camadas L1 a L5

Cada camada fecha com critério de aceite testável no hardware e com a
suíte de host verde. Nada de L(n+1) começa com pendência de L(n)
(disciplina herdada da v1, onde funcionou por 57 commits).

## L1 — Fundação (v2.0.0) — ✅ ACEITA EM HARDWARE 2026-10-23

- [x] BSP reconciliado (display/touch/storage) como componente isolado
- [x] Config + scripts Lua protegidos como componente (ABI 32-bit guardada)
- [x] Energia em degraus + sessão de hibernação
- [x] Arquivos (navegar/operar) + notas + editor (cursor/OSK/markdown)
- [x] Teclado USB HID; status bar; quick panel; tema M3 + acentos
- [x] Camada app modular (1 tela = 1 módulo) + composition root
- [x] Suíte de host: cfg/uigate/lua-bind/md/syntaxcheck/render offscreen
- **Aceite:** ✅ 2026-10-23 (checklist Parte A completo; serial em
  docs/BUILD_LOG.md §Rodada de validação).

## L2 — Conectividade (Wi-Fi + NTP via C6) — ✅ ACEITA EM HARDWARE 2026-10-23 (v2.1.1; B10 seed de relógio FECHADO 2026-10-24: com rádio off, `os.date()` no Lua devolveu 2026-10-24 21:00:05 BRT / epoch 1792886405 (= mesmo instante em UTC) — seed do NVS confirmado; status bar em uptime até sync é decisão de projeto)

- [x] `components/pda_net` (wifi_net da v1: scan-driven, multi-redes,
      reason por nome, backoff) + tela Redes + chave de rádio na Config
- [ ] NTP + relógio real na status bar (seed de RTC via NVS no boot)
- [ ] QuickPanel com linha de rede + atalho Redes
- [ ] Host tests: parser de wifi.lua + máquina de reconexão (stub de evento)
- **Aceite:** ✅ 2026-10-23 (checklist Parte B, B1–B9+B11; B10 seed de
  relógio com procedimento entregue ao usuário, reporte pendente).

## L3 — Áudio — SOM.1–SOM.3 ✅ em HW (v2.2.3); SOM.4 pendente de HW

- [x] `components/pda_audio`: SOM.1 player WAV no falante local (I2S/
      ES8311) com `wav_parse.[ch]` compartilhado + testes de host com
      WAVs sintéticos (fmt estendido, chunks ímpares, LIST/FACT)
- [x] SOM.2 roteamento interno|UAC|auto (RAM-only; persistência = SOM.6)
      · SOM.3 volume local no codec + mute (NVS por fonte, v2.2.1)
- [x] Tela Música (rota na tela) + media keys HID → player + sessão "music"
- [x] SOM.4 (v2.3.0): MP3 via minimp3 (CC0, vendorizado), playlists
      .m3u/.m3u8, metadados ID3v2.2/3/4 + LIST/INFO, fila de reprodução,
      `pcm_src` como fonte única dos dois backends
- [x] v2.3.1: hotfix dos 3 erros de build do alvo (GCC 14) + rede de
      proteção nova no host (`-Werror` do GCC 14 no syntaxcheck,
      `stubs-i2s/` p/ audio_i2s.c, `msg_budget.py`) — F24
- **Aceite SOM.1–SOM.3:** ✅ rodadas C1–C15 (v2.2.0), R1–R6 (v2.2.1/2),
  W1–W4 (v2.2.3): WAV 44k1/48k no falante e no UAC1, pause/resume,
  beep×música, rota/handover, volume NVS inclusive no 1º play.
- **Aceite SOM.4:** ⏳ checklist M1–M7, reemitido em
  `release/v2.3.1/CHECKLIST_HW_v2.3.1.md` (a v2.3.0 não compilou no
  alvo, então nunca chegou a ser flashada).
- [ ] C12 (UAC2) adiado por ambiente: porta HS sem VBUS em bateria e
      `usb_host_uac` 1.5.0 é UAC1-only (docs/USB.md).

## L4 — Bateria

- [ ] `components/pda_battery` (divisor GPIO53, máquina de estados,
      âncoras NVS) + `pda.battery.*` de volta + scripts de campo
- [ ] BAT.2b debounce do flap saiu/volta · BAT.1b forma da curva (com o
      CSV de campo da v1 como referência) · BAT.3 autonomia · BAT.5 saúde
- **Aceite:** % estável sob carga (EMA τ64s) e escada de 1pt/12s na
  bateria; linha Config→Sistema ao vivo sem serial.

## L5 — Apps de PDA + conforto

- [ ] Agenda/Contatos/Relógio (alarmes) com dados em Lua/CSV no cartão;
      alarme ↔ áudio (L3) e wake agendado
- [ ] MTP/MSC explícito na Config (armazenamento visível no PC — exige
      troca de papel USB dinâmica; milestone próprio na v1, segue sendo)
- [ ] Virtualização de listas do editor (arquivos com milhares de linhas)
- [ ] OTA (slot na reserva de ~1,9 MB do flash)

## Dívidas técnicas conhecidas (herdadas, vivas)

- Medição real de corrente por degrau (`POWER.md` → plano de medição)
- Teardown completo do DSI no standby se light sleep passar de ~40 mA
- Validação do INT do GT911 (GPIO21) p/ `wake_on_touch`
- M-power (rework do light sleep): caminho atrás do kill-switch; wake
  remount SDMMC×SDIO/hosted causava Instruction access fault no core 1
- USB primeira enumeração (`EP 0 Error` na ramp de energia; retry do IDF
  recupera) — retry próprio de enumeração candidatado desde a v1
- Backlog de gestos/UI: `docs/UI_WISHLIST.md` da v1 segue como referência

## Princípios de evolução

1. Camada nova = componente novo (fronteira compilada), nunca código
   espalhado na camada app.
2. Toda lógica pura nasce com teste de host (REGRESSOES_BUILD da v1).
3. UI nova nasce com render offscreen antes da placa.
4. Mudança de pin/version/ABI exige medição ou evidência (esquemático,
   log de campo, teste) — comentários de causa raiz não se apagam.

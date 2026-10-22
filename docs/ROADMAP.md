# ROADMAP do p4da-v2 — camadas L1 a L5

Cada camada fecha com critério de aceite testável no hardware e com a
suíte de host verde. Nada de L(n+1) começa com pendência de L(n)
(disciplina herdada da v1, onde funcionou por 57 commits).

## L1 — Fundação (ESTE release, v2.0.0)

- [x] BSP reconciliado (display/touch/storage) como componente isolado
- [x] Config + scripts Lua protegidos como componente (ABI 32-bit guardada)
- [x] Energia em degraus + sessão de hibernação
- [x] Arquivos (navegar/operar) + notas + editor (cursor/OSK/markdown)
- [x] Teclado USB HID; status bar; quick panel; tema M3 + acentos
- [x] Camada app modular (1 tela = 1 módulo) + composition root
- [x] Suíte de host: cfg/uigate/lua-bind/md/syntaxcheck/render offscreen
- **Aceite (1ª rodada de placa — checklist em docs/BUILD_LOG.md §final):**
  boot com/sem cartão; standby idle+manual com wake por botão; hibernar
  e restaurar nota; editar via OSK e via USB; scripts exemplo+logger;
  ops de arquivo; pulldown de brilho; gestos (voltar/opt-out do editor).

## L2 — Conectividade (Wi-Fi + NTP via C6)

- [ ] `components/pda_net` (wifi_net da v1: scan-driven, multi-redes,
      reason por nome, backoff) + tela Redes + chave de rádio na Config
- [ ] NTP + relógio real na status bar (seed de RTC via NVS no boot)
- [ ] QuickPanel com linha de rede + atalho Redes
- [ ] Host tests: parser de wifi.lua + máquina de reconexão (stub de evento)
- **Aceite:** conecta em rede salva no boot; AP que some e volta reconecta
  sem re-salvar (incidente da 14ª rodada da v1); scan com rádio off avisa.

## L3 — Áudio

- [ ] `components/pda_audio`: SOM.1 player WAV no falante local (I2S/
      ES8311) com `wav_parse.[ch]` compartilhado + testes de host com
      WAVs sintéticos (fmt estendido, chunks ímpares, LIST/FACT)
- [ ] SOM.2 roteamento interno|UAC|auto · SOM.3 volume local
- [ ] Tela Música + media keys HID → player + sessão "music"
- **Aceite:** WAV 44k1 e 48k no falante e no fone UAC1; pause/resume com
  posição; beep não empilha com música (mutex + handshake).

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

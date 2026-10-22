# p4da-v2 — PDA de bolso em ESP32-P4 (fundação L1)

Sistema operacional de bolso para a **GUITION JC4880P443C_I_W**
(ESP32-P4 + ESP32-C6, tela 4.3" 480×800 MIPI-DSI, touch GT911, SD,
ES8311 + alto-falante, bateria via divisor GPIO53), em C++/Slint + Lua 5.5.

O v2 é uma reconstrução **quase do zero** do
[`Andre-Wa/p4da-v1`](https://github.com/Andre-Wa/p4da-v1) (v5.16.2): o que
estava provado em hardware foi **carregado** (BSP, energia, VM Lua, design
system Slint), o que estava errado foi **corrigido** (acoplamentos/ciclos
de dependência, monolito de 2141 linhas), e o que não existia foi
**desenhado** (componentes ESP-IDF com fronteiras de camada, verificação
de host sem hardware, camadas de evolução L2–L5).

## Escopo da primeira versão (camada L1 — fundação)

- Tela/touch/backlight com rotação paisagem (Slint 1.12.1, software renderer)
- **Cartão SD como armazenamento principal** (SDMMC 4-bit @ 40 MHz) com
  fallback interno transparente; árvore `/pda/{config,scripts,notes,music,logs,.state}`
- **Configurações em Lua** (`config/system.lua`) editáveis na tela ou no cartão
- **Scripts Lua** (`scripts/*.lua`) com console na tela e API `pda.*` protegida
  (20M instruções, 2 MB de VM, `lua_pcall` sempre)
- Gerenciador de arquivos navegável + operações (renomear/copiar/mover/apagar,
  picker de destino, prompt com cursor)
- Notas + **editor com cursor real, teclado virtual e leitura markdown**
- **Energia em degraus**: DIM → STANDBY (acorda por botão/toque) → HIBERNATE
  (deep sleep com sessão restaurada do SD)
- Teclado USB HID (boot protocol) com mapa US + shift
- Status bar (relógio de uptime na L1, armazenamento, estado de energia) +
  pulldown de ajustes rápidos com brilho ao vivo
- Tema Material You dark com 5 paletas de acento (M3 adaptado)

**Fora da L1 (camadas seguintes — `docs/ROADMAP.md`):** Wi-Fi/NTP (L2),
áudio UAC/I2S + player (L3), leitor de bateria + `pda.battery.*` (L4),
apps de PDA/OTA (L5). Os tiles cinzas no launcher são o contrato visual.

## Build

Requer **ESP-IDF v5.5.x** no PATH (pinado de propósito — ver
`docs/HARDWARE.md` achado #5 e `docs/DEPENDENCIAS.md`).

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor     # porta USB-Serial-JTAG ("Full Speed")
```

Primeiro build baixa os componentes gerenciados (Slint, Lua, drivers LCD/
touch, LittleFS, HID) via Component Manager — tudo pinado `==`.

### Cartão SD

```bash
cp -r sdcard-template/pda /mnt/seu-cartao/
```

Sem cartão, tudo funciona igual em `/internal/pda` (8 MB) — e as
configurações são promovidas ao cartão no primeiro boot com ele presente.

## Testes sem hardware (disciplina da v1, ampliada)

```bash
bash tools/hosttest/run.sh      # pda_config + ui_gate + lua_runtime REAIS no host
bash tools/syntaxcheck/run.sh   # typecheck da camada app vs app_ui.h gerado
g++ -std=c++17 -I main tools/md_test.cpp -o /tmp/mdtest && /tmp/mdtest
SLINT_COMPILER=... SLINT_CPP=... bash tools/render_offscreen.sh   # 11 PNGs
```

Detalhes e resultados em `docs/TESTES.md` e `docs/BUILD_LOG.md`.

## Estrutura

```
p4da-v2/
├── CMakeLists.txt · partitions.csv · sdkconfig.defaults
├── components/                  # serviços e BSP — NÃO conhecem a camada app
│   ├── pda_board/               # BSP: board_config, display, touch, storage
│   ├── pda_script/              # VM Lua protegida + config (LUA_32BITS aqui)
│   ├── pda_power/               # degraus de energia (POWER.md)
│   └── pda_usb/                 # USB Host HID (host lib instalada aqui)
├── main/                        # camada app: 1 tela = 1 módulo
│   ├── app_main.cpp             #   composition root (bring-up + injeções)
│   ├── app_context.[ch]         #   contrato interno (ui_post, threads, nav)
│   ├── screens.h                #   entradas públicas por tela
│   ├── screen_*.cpp · status_bar.cpp · session.cpp · input_usb.cpp
│   └── ui/                      #   Slint: theme + elements + screens
├── sdcard-template/pda/         # seed do cartão (config/notas/scripts)
├── tools/                       # hosttest · syntaxcheck · render offscreen
└── docs/                        # ARCHITECTURE · BUILD_LOG · MIGRATION ·
                                 # ROADMAP · TESTES + docs de domínio
```

## Leia antes de flashear

| Doc | Conteúdo |
|---|---|
| `docs/ARCHITECTURE.md` | camadas, threading, armazenamento, decisões e porquês |
| `docs/BUILD_LOG.md` | a construção fase a fase (F0–F6) + validações |
| `docs/MIGRATION.md` | mapa v1→v2 arquivo a arquivo + como religar L2–L4 |
| `docs/ROADMAP.md` | camadas L1–L5 com critérios de aceite |
| `docs/HARDWARE.md` | pinagem reconciliada + achados críticos de campo |
| `docs/POWER.md` | degraus de energia + plano de medição |
| `docs/LUA.md` | formato da config + referência da API `pda.*` |
| `docs/TESTES.md` | suíte de host (o que cada teste prova) |

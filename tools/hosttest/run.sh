#!/usr/bin/env bash
# Testes de host do p4da-v2 (código REAL dos componentes + stubs de ESP).
# Uso: tools/hosttest/run.sh
#
# v2/L1: seções da v1 — cfg (pda_config.c), uigate (ui_gate.h), lua-bind
# (lua_runtime.c + script real). A seção battery (host_batt_test.c) volta
# na camada L4. Caminhos atualizados p/ a estrutura de componentes.
set -e
cd "$(dirname "$0")/../.."

# raízes fake: /sdcard e /internal apontam p/ /tmp/cfgtest (censo/stat reais)
rm -rf /tmp/cfgtest
mkdir -p /tmp/cfgtest/sdcard/pda/config /tmp/cfgtest/internal/pda/config
for link in /sdcard /internal; do
    [ -e "$link" ] && [ ! -L "$link" ] && { echo "ERRO: $link existe e não é symlink"; exit 2; }
    ln -sfn "/tmp/cfgtest${link}" "$link"
done

LUA_INC="${LUA_INC:-/usr/include/lua5.4}"
LUA_LIB="${LUA_LIB:-lua5.4}"

# ---- CFG: pda_config.c REAL (parse/serialize/sanitize/sombra NVS) ----
gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
    -I tools/hosttest/stubs -I components/pda_script/include -I "$LUA_INC" \
    components/pda_script/pda_config.c tools/hosttest/host_cfg_test.c \
    -l"$LUA_LIB" -lm -o /tmp/cfgtest_bin

echo "--- rodando /tmp/cfgtest_bin ---"
/tmp/cfgtest_bin

# ---- UI-LOOP-GATE (v5.16.0): ui_gate.h real, invoker mock (crash da 15ª
# rodada da v1: invoke_from_event_loop antes do loop = assert xTaskToNotify) ----
g++ -std=c++17 -Wall -Wextra -pthread -I main \
    tools/hosttest/host_uigate_test.cpp -o /tmp/uigate_bin

echo "--- rodando /tmp/uigate_bin ---"
/tmp/uigate_bin

# ---- LUA-BIND: lua_runtime.c REAL no Lua 5.4 do host (shim lua55_shim.h
# casa o lua_newstate de 3 args da 5.5) + o script REAL logger_exemplo.lua
# rodado end-to-end contra storage/relógio em stdio — o CSV nasce e é
# afirmado aqui, não só no alvo. ----
rm -rf /tmp/luabind
gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -include tools/hosttest/lua55_shim.h \
    -I tools/hosttest/stubs -I components/pda_script/include -I "$LUA_INC" \
    components/pda_script/lua_runtime.c tools/hosttest/host_lua_test.c \
    -l"$LUA_LIB" -lm -o /tmp/luabind_bin

echo "--- rodando /tmp/luabind_bin ---"
/tmp/luabind_bin

# ---- WAV-PARSE (L3/SOM.1): wav_parse.c REAL (lógica pura) contra WAVs
# sintéticos gerados pelo teste (fmt ext, LIST ímpar+FACT, não-PCM...). ----
mkdir -p /tmp/wavtest
gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter     -I components/pda_audio/include     components/pda_audio/wav_parse.c tools/hosttest/host_wav_test.c     -o /tmp/wavtest_bin

echo "--- rodando /tmp/wavtest_bin ---"
/tmp/wavtest_bin

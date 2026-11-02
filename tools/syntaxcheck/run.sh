#!/usr/bin/env bash
# syntaxcheck/run.sh — checagem de SINTAXE+TIPOS da camada app no host,
# sem ESP-IDF. Novo na v2 (a v1 validava main.cpp só no alvo).
#
# O que é REAL aqui (pega drift de API de verdade):
#   - app_ui.h GERADO pelo slint-compiler 1.12.1 a partir do .slint real
#     (o mesmo contrato que o build do alvo gera);
#   - headers do Slint-cpp 1.12.1 Linux (slint.h/slint-platform.h — API
#     idêntica ao prebuilt riscv32 do alvo);
#   - headers PÚBLICOS dos componentes (components/*/include) e
#     md_render.h/ui_gate.h do main.
# O que é stub (só o sistema, não a nossa API):
#   - esp_log/esp_timer/freertos/nvs/esp_lcd*/driver (tools/syntaxcheck/stubs);
#   - slint-esp.h (superfície de init do backend ESP).
#
# Cobre main/*.cpp e as fontes de componente listadas no estágio 2 com
# -O2 -c -o /dev/null (não linka — externs não resolvidos são esperados).
# Lógica pura tem teste próprio em tools/hosttest.
#
# Uso: tools/syntaxcheck/run.sh
# Env: SLINT_COMPILER, SLINT_CPP (defaults: ~/.tools, como o env da v1)
set -e
cd "$(dirname "$0")/../.."

SLINT_COMPILER="${SLINT_COMPILER:-$HOME/.tools/slint-compiler}"
SLINT_CPP="${SLINT_CPP:-$HOME/.tools/Slint-cpp-1.12.1-Linux-x86_64}"
GEN="${GEN:-/tmp/p4da_v2_gen}"

[ -x "$SLINT_COMPILER" ] || { echo "ERRO: slint-compiler não achado em $SLINT_COMPILER"; exit 1; }
[ -d "$SLINT_CPP/include/slint" ] || { echo "ERRO: Slint-cpp não achado em $SLINT_CPP"; exit 1; }

echo "== gerando app_ui.h (slint-compiler 1.12.1) =="
rm -rf "$GEN" && mkdir -p "$GEN"
"$SLINT_COMPILER" --format cpp -o "$GEN/app_ui.h" main/ui/app_ui.slint

INCLUDES=(
    -I tools/syntaxcheck/stubs
    -I main
    -I "$GEN"
    -I "$SLINT_CPP/include/slint"
    -I components/pda_board/include
    -I components/pda_script/include
    -I components/pda_power/include
    -I components/pda_usb/include
    -I components/pda_net/include
    -I components/pda_audio/include
)

fail=0

# v2.3.1: -O2 -c (em vez de -fsyntax-only) + as flags de formato como ERRO.
# Motivo: o build da v2.3.0 quebrou no alvo em -Werror=format-truncation, e
# esse diagnóstico SÓ existe com otimização (o snprintf precisa ser inlinado
# no chamador). Custa ~3 s/TU a mais; é o preço de ver o que o alvo vê.
# -Werror=format: o stub de esp_log.h roteia p/ printf (que tem o
# atributo de formato), então a conferência de "%lu × uint32_t" etc. vale
# aqui igual ao alvo.
FORMAT_STRICT=(-Werror=format -Werror=format-truncation -Werror=format-overflow)

echo "== g++ (gnu++20, -Wall -Wextra; -O2 + formato=erro nos TUs c/ snprintf) =="
# -Wno-missing-field-initializers: o IDF já traz essa flag nos defaults do
# alvo (designated initializers parciais são idiomáticos no esp_lcd/slint).
for src in main/*.cpp; do
    log="/tmp/syntaxcheck_$(basename "$src" .cpp).log"
    # O passe -O2 existe SÓ p/ -Wformat-truncation/-Wformat-overflow (esses
    # diagnósticos exigem inlining). TU sem snprintf não tem o que ganhar
    # nele — e no sandbox app_main.cpp (composition root, Slint pesado) é
    # morto por falta de memória já no -O1. Escolha automática, sem lista
    # hardcoded para apodrecer.
    OPT="-fsyntax-only"
    if grep -qE '\bv?snprintf[[:space:]]*\(' "$src"; then OPT="-O2 -c -o /dev/null"; fi
    # shellcheck disable=SC2086
    if g++ -std=gnu++20 $OPT -Wall -Wextra -Wno-unused-parameter \
           -Wno-missing-field-initializers "${FORMAT_STRICT[@]}" \
           "${INCLUDES[@]}" "$src" 2>"$log"; then
        # warnings (não-erro) ainda importam: mostra mas não reprova
        [ -s "$log" ] && { echo "  OK* $src (warnings:)"; sed 's/^/      /' "$log"; } \
                    || echo "  OK  $src"
    else
        echo "  FAIL $src"; sed 's/^/      /' "$log"; fail=1
    fi
done

# ---- estágio 2 (v2.2.2): FONTES DE COMPONENTES contra headers REAIS do
# IDF 5.5.1 (usb/) baixados em real/ — pega drift de API de verdade (foi
# assim que o watcher v2.2.1 passou errado: API inventada).
# hid_host.h é stub derivado do uso v1 (repo não browsável); uac_host.h é
# o real do componente 1.5.0 (cópia local).
#
# v2.3.1 — SEVERIDADE DO ALVO: o GCC do IDF 5.5 é o 14, que promove
# -Wincompatible-pointer-types e -Wint-conversion a ERRO por padrão; o GCC
# 12 deste sandbox só avisa. Foi exatamente assim que a v2.3.0 passou aqui
# e morreu no alvo (uac_host_device_write() recebe uint8_t*, o pcm_src dá
# int16_t*). As flags abaixo replicam o GCC 14, e -O2 liga o inlining de
# que o -Wformat-truncation precisa.
echo "== componentes vs headers IDF reais (usb), severidade GCC 14 =="
REAL=tools/syntaxcheck/real
# comuns a C e C++
TARGET_STRICT=(
    -Werror=format
    -Werror=return-type
    -Werror=format-truncation
    -Werror=format-overflow
)
# só C: no C++ essas conversões já são erro de tipagem (e as flags não
# existem — passá-las ao g++ só gera ruído "not valid for C++")
C_ONLY_STRICT=(
    -Werror=incompatible-pointer-types
    -Werror=int-conversion
    -Werror=implicit-function-declaration
    -Werror=implicit-int
)
for src in components/pda_audio/audio_uac.c components/pda_usb/usb_hid_keyboard.cpp \
         components/pda_audio/audio_decode.c components/pda_audio/audio_tags.c \
         components/pda_audio/audio_playlist.c components/pda_audio/audio_route.c \
         components/pda_audio/wav_parse.c components/pda_audio/audio_vol.c \
         components/pda_audio/audio_i2s.c; do
    log="/tmp/syntaxcheck_comp_$(basename "$src").log"
    CC=g++; STD=-std=gnu++20; EXTRA_STRICT=()
    if [ "${src##*.}" = c ]; then
        CC=gcc; STD=-std=gnu11; EXTRA_STRICT=("${C_ONLY_STRICT[@]}")
    fi
    if $CC "$STD" -O2 -c -o /dev/null -Wall -Wextra -Wno-unused-parameter \
           -Wno-missing-field-initializers "${TARGET_STRICT[@]}" "${EXTRA_STRICT[@]}" \
           -I tools/syntaxcheck/stubs-i2s -I tools/syntaxcheck/stubs -I "$REAL" \
           -I components/pda_board/include -I components/pda_script/include \
           -I components/pda_power/include -I components/pda_usb/include \
           -I components/pda_audio/include -I components/pda_audio/minimp3 \
           "$src" 2>"$log"; then
        # warning não reprova, mas APARECE (foi um warning de unused-variable
        # que o alvo mostrou junto com o erro da v2.3.0 — ruído que quero ver)
        [ -s "$log" ] && { echo "  OK* $src (componente, warnings:)"; sed 's/^/      /' "$log"; } \
                    || echo "  OK  $src (componente)"
    else
        echo "  FAIL $src"; sed 's/^/      /' "$log"; fail=1
    fi
done
# audio_i2s.c entra desde a v2.3.1 via stubs-i2s/ (assinaturas derivadas do
# uso — pegam erro de FORMA, não drift de contrato; ver stubs-i2s/README.md).
# A classe que quebrou a v2.3.0 (literal maior que s_state) tem garantia
# própria e determinística: tools/hosttest/msg_budget.py.

[ $fail -eq 0 ] && echo "SYNTAXCHECK: TUDO PASSOU" || echo "SYNTAXCHECK: FALHOU"
exit $fail

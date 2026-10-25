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
# Cobre main/*.cpp com -fsyntax-only (não linka — externs não resolvidos
# são esperados). Lógica pura tem teste próprio em tools/hosttest.
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
echo "== g++ -fsyntax-only (gnu++20, -Wall -Wextra) =="
# -Wno-missing-field-initializers: o IDF já traz essa flag nos defaults do
# alvo (designated initializers parciais são idiomáticos no esp_lcd/slint).
for src in main/*.cpp; do
    log="/tmp/syntaxcheck_$(basename "$src" .cpp).log"
    if g++ -std=gnu++20 -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
           -Wno-missing-field-initializers \
           "${INCLUDES[@]}" "$src" 2>"$log"; then
        # warnings (não-erro) ainda importam: mostra mas não reprova
        [ -s "$log" ] && { echo "  OK* $src (warnings:)"; sed 's/^/      /' "$log"; } \
                    || echo "  OK  $src"
    else
        echo "  FAIL $src"; sed 's/^/      /' "$log"; fail=1
    fi
done

[ $fail -eq 0 ] && echo "SYNTAXCHECK: TUDO PASSOU" || echo "SYNTAXCHECK: FALHOU"
exit $fail

#!/usr/bin/env bash
# syntaxcheck/run.sh — checagem de SINTAXE+TIPOS no host, sem ESP-IDF.
# Novo na v2 (a v1 validava main.cpp só no alvo).
#
# O que é REAL aqui (pega drift de API de verdade):
#   - app_ui.h GERADO pelo slint-compiler 1.12.1 a partir do .slint real
#     (o mesmo contrato que o build do alvo gera);
#   - headers do Slint-cpp 1.12.1 Linux (slint.h/slint-platform.h — API
#     idêntica ao prebuilt riscv32 do alvo);
#   - headers PÚBLICOS dos componentes (components/*/include), os headers
#     REAIS do IDF 5.5.1 em real/usb/ e md_render.h/ui_gate.h do main.
# O que é stub (só o sistema, não a nossa API):
#   - esp_log/esp_timer/freertos/nvs/esp_lcd*/driver (tools/syntaxcheck/stubs);
#   - I2S + esp_codec_dev (tools/syntaxcheck/stubs-i2s — ver o README);
#   - slint-esp.h (superfície de init do backend ESP).
#
# Severidade: os dois estágios usam as flags que o GCC 14 do IDF 5.5 promove
# a ERRO (v2.3.1) e otimizam quando o diagnóstico exige inlining. Um TU que
# o host não consegue otimizar por falta de memória é refeito com
# -fsyntax-only e marcado como OK* com aviso — falha de AMBIENTE não vira
# falso positivo de código (e nem passa em silêncio: a nota diz o que não
# foi checado).
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

COMP_INCLUDES=(
    -I tools/syntaxcheck/stubs-i2s
    -I tools/syntaxcheck/stubs
    -I tools/syntaxcheck/real
    -I components/pda_board/include
    -I components/pda_script/include
    -I components/pda_power/include
    -I components/pda_usb/include
    -I components/pda_audio/include
    -I components/pda_audio/minimp3
)

# -Wno-missing-field-initializers: o IDF já traz essa flag nos defaults do
# alvo (designated initializers parciais são idiomáticos no esp_lcd/slint).
#
# compile_tu <cc> <std> <flags-de-otimização> <log> <fontes/includes...>
#   usa o array global STRICT; devolve 0/1 e, em caso de OOM do host, refaz
#   sem otimização e preenche OOM_NOTE com o que deixou de ser checado.
STRICT=()
OOM_NOTE=""
compile_tu() {
    local cc="$1" std="$2" opt="$3" log="$4"
    shift 4
    OOM_NOTE=""
    # shellcheck disable=SC2086
    if "$cc" "$std" $opt -Wall -Wextra -Wno-unused-parameter \
          -Wno-missing-field-initializers "${STRICT[@]}" "$@" 2>"$log"; then
        return 0
    fi
    if grep -qE 'Killed|virtual memory exhausted|Cannot allocate memory' "$log"; then
        # OOM pode ser transitório (outro TU grande recém-liberado): espera
        # e tenta de novo antes de desistir. Sem otimização em ambos. E o
        # último degrau troca tempo por memória (GC agressivo do GCC) — em
        # host de ~700 MB livres é o que atravessa o código gerado do Slint.
        local degraus=("" "--param ggc-min-expand=2 --param ggc-min-heapsize=8192")
        local d tentativa
        for tentativa in 1 2; do
            for d in "${degraus[@]}"; do
                sleep 3
                # shellcheck disable=SC2086
                if "$cc" "$std" -fsyntax-only $d -Wall -Wextra -Wno-unused-parameter \
                      -Wno-missing-field-initializers "${STRICT[@]}" "$@" 2>"$log"; then
                    OOM_NOTE=" (host sem RAM p/ -O2: checagem de FORMATO não rodou aqui)"
                    return 0
                fi
                grep -qE 'Killed|virtual memory exhausted|Cannot allocate memory' "$log" || return 1
            done
        done
    fi
    return 1
}

fail=0

# v2.3.1: -O2 -c (em vez de -fsyntax-only) + as flags de formato como ERRO.
# Motivo: o build da v2.3.0 quebrou no alvo em -Werror=format-truncation, e
# esse diagnóstico SÓ existe com otimização (o snprintf precisa ser inlinado
# no chamador). Custa ~3 s/TU a mais; é o preço de ver o que o alvo vê.
# -Werror=format: o stub de esp_log.h roteia p/ printf (que tem o atributo
# de formato), então a conferência de "%lu × uint32_t" vale aqui igual ao alvo.
STRICT=(-Werror=format -Werror=format-truncation -Werror=format-overflow)

echo "== estágio 1: main/*.cpp (gnu++20, -O2 + formato=erro nos TUs c/ snprintf) =="
for src in main/*.cpp; do
    log="/tmp/syntaxcheck_$(basename "$src" .cpp).log"
    # O passe -O2 existe SÓ p/ -Wformat-truncation/-Wformat-overflow (esses
    # diagnósticos exigem inlining). TU sem snprintf não tem o que ganhar
    # nele — e no sandbox app_main.cpp (composition root, Slint pesado) é
    # morto por falta de memória já no -O1. Escolha automática, sem lista
    # hardcoded para apodrecer.
    OPT="-fsyntax-only"
    if grep -qE '\bv?snprintf[[:space:]]*\(' "$src"; then OPT="-O2 -c -o /dev/null"; fi
    if compile_tu g++ -std=gnu++20 "$OPT" "$log" "${INCLUDES[@]}" "$src"; then
        # warnings (não-erro) ainda importam: mostra mas não reprova
        [ -s "$log" ] && { echo "  OK* $src (warnings:)"; sed 's/^/      /' "$log"; } \
                    || echo "  OK  $src$OOM_NOTE"
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
echo "== estágio 2: componentes vs headers IDF reais (usb), severidade GCC 14 =="
COMMON_STRICT=(-Werror=format -Werror=return-type -Werror=format-truncation -Werror=format-overflow)
# só C: no C++ essas conversões já são erro de tipagem (e as flags não
# existem — passá-las ao g++ só gera ruído "not valid for C++")
C_ONLY_STRICT=(-Werror=incompatible-pointer-types -Werror=int-conversion
               -Werror=implicit-function-declaration -Werror=implicit-int)

for src in components/pda_audio/audio_uac.c components/pda_usb/usb_hid_keyboard.cpp \
         components/pda_audio/audio_decode.c components/pda_audio/audio_tags.c \
         components/pda_audio/audio_playlist.c components/pda_audio/audio_route.c \
         components/pda_audio/audio_scan.c components/pda_audio/wav_parse.c \
         components/pda_audio/audio_vol.c components/pda_audio/audio_i2s.c; do
    log="/tmp/syntaxcheck_comp_$(basename "$src").log"
    if [ "${src##*.}" = c ]; then
        CC=gcc; STD=-std=gnu11; STRICT=("${COMMON_STRICT[@]}" "${C_ONLY_STRICT[@]}")
    else
        CC=g++; STD=-std=gnu++20; STRICT=("${COMMON_STRICT[@]}")
    fi
    if compile_tu "$CC" "$STD" "-O2 -c -o /dev/null" "$log" "${COMP_INCLUDES[@]}" "$src"; then
        # warning não reprova, mas APARECE (foi um warning de unused-variable
        # que o alvo mostrou junto com o erro da v2.3.0 — ruído que quero ver)
        [ -s "$log" ] && { echo "  OK* $src (componente, warnings:)"; sed 's/^/      /' "$log"; } \
                    || echo "  OK  $src (componente)$OOM_NOTE"
    else
        echo "  FAIL $src"; sed 's/^/      /' "$log"; fail=1
    fi
done
# audio_i2s.c entra desde a v2.3.1 via stubs-i2s/ (assinaturas derivadas do
# uso — pegam erro de FORMA, não drift de contrato; ver stubs-i2s/README.md).
# A classe que quebrou a v2.3.0 (literal maior que s_state) tem garantia
# própria e determinística: tools/hosttest/msg_budget.py.
# pda_board/pda_net/pda_power/pda_script seguem fora: cadeia de headers
# grande demais p/ vendorizar e nada nelas mudou desde que o alvo validou.

[ $fail -eq 0 ] && echo "SYNTAXCHECK: TUDO PASSOU" || echo "SYNTAXCHECK: FALHOU"
exit $fail

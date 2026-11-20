#!/usr/bin/env bash
# render_offscreen.sh — valida a UI SEM hardware (mesmo backend do device).
#
# Compila main/ui/app_ui.slint com o slint-compiler 1.12.1 em modo
# `embed-for-software-renderer`, linka com o Slint-cpp de Linux e renderiza
# os modos do harness (reading, reading2, edit, networks, prompt, panel,
# paneldrag, settings, settings2, launcher, coltest, fontpitch) para PNG
# em docs/img/. v2.1.0: networks de volta (L2); music segue p/ L3.
#
# Pré-requisitos no host (Linux x86_64):
#   - slint-compiler 1.12.1   (release do GitHub, binário único)
#   - Slint-cpp 1.12.1        (tarball Linux-x86_64: include/slint + libslint_cpp.so)
#   - g++ C++20
#   - python3 com PIL (só p/ converter PPM->PNG)
#
# Uso:
#   SLINT_COMPILER=/caminho/slint-compiler SLINT_CPP=/caminho/Slint-cpp-1.12.1 \
#     tools/render_offscreen.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SLINT_COMPILER="${SLINT_COMPILER:-$HOME/.tools/slint-compiler}"
SLINT_CPP="${SLINT_CPP:-$HOME/.tools/Slint-cpp-1.12.1-Linux-x86_64}"
WORK="${WORK:-/tmp/harness}"

[ -x "$SLINT_COMPILER" ] || { echo "ERRO: slint-compiler não achado em $SLINT_COMPILER"; exit 1; }
[ -f "$SLINT_CPP/lib/libslint_cpp.so" ] || { echo "ERRO: libslint_cpp.so não achada em $SLINT_CPP/lib"; exit 1; }

mkdir -p "$WORK" "$REPO/docs/img"
cp "$REPO/tools/offscreen_render_harness.cpp" "$WORK/main.cpp"
cd "$WORK"

echo "== slint-compiler (embed p/ software renderer) =="
"$SLINT_COMPILER" --embed-resources embed-for-software-renderer \
    -o gen.h --cpp-file gen_a.cpp --cpp-file gen_b.cpp \
    "$REPO/main/ui/app_ui.slint"

# Host com pouca RAM (o sandbox onde isto é escrito tem ~700 MB livres) não
# atravessa o código gerado do Slint em -O1. Os degraus abaixo trocam tempo
# por memória — e NENHUM deles muda o resultado do render (só a velocidade):
#   -O1  →  -O0  →  -O0 com GC agressivo do GCC (--param ggc-min-*).
# Sempre avisamos quando caímos de degrau, para ninguém achar que foi o
# build padrão que rodou.
cxx() {
    local out="$1"; shift
    local degraus=("-O1" "-O0" "-O0 --param ggc-min-expand=2 --param ggc-min-heapsize=8192")
    local d
    for d in "${degraus[@]}"; do
        # shellcheck disable=SC2086
        if g++ -std=c++20 $d -I"$SLINT_CPP/include/slint" -I"$REPO/main" -I. \
               "$@" -o "$out" 2>/tmp/render_cc.log; then
            [ "$d" = "-O1" ] || echo "  (pouca RAM: $(basename "$out") em '$d' — render idêntico)"
            return 0
        fi
        grep -qE 'Killed|virtual memory exhausted|Cannot allocate memory' /tmp/render_cc.log || break
    done
    echo "  FALHA compilando $out:"; sed 's/^/    /' /tmp/render_cc.log; exit 1
}

echo "== g++ =="
cxx gen_a.o -c gen_a.cpp
cxx gen_b.o -c gen_b.cpp
cxx harness main.cpp gen_a.o gen_b.o -L"$SLINT_CPP/lib" -lslint_cpp -Wl,-rpath,"$SLINT_CPP/lib"

echo "== render =="
# coltest/fontpitch = sondas M4.13 (v1) do pitch de glifo (drift do
# cursor-bloco): coltest compara overlay vetorial vs glifo in-flow na
# col 43; fontpitch mede o avanço real por (face,tamanho) com 24 'M'.
for mode in reading reading2 edit networks music prompt panel paneldrag settings settings2 launcher coltest fontpitch; do
    ./harness "$mode"
    python3 - "$mode" "$REPO" <<'PY'
import sys
from PIL import Image
mode, repo = sys.argv[1], sys.argv[2]
Image.open(f"/tmp/harness/render_{mode}.ppm").save(f"{repo}/docs/img/render_{mode}.png")
print(f"  -> {repo}/docs/img/render_{mode}.png")
PY
done
echo "== ok: abra docs/img/render_reading.png / render_edit.png / render_launcher.png =="

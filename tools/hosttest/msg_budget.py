#!/usr/bin/env python3
"""msg_budget.py — orçamento das mensagens de estado do áudio (v2.3.1).

POR QUE EXISTE
--------------
O build da v2.3.0 no alvo morreu em:

    audio_i2s.c:79:41: error: '%s' directive output truncated writing 53
    bytes into a region of size 48 [-Werror=format-truncation=]

A mensagem nova do SOM.4 ("erro: formato/taxa não suportados
(WAV/MP3 44k1/48k)") tem 53 BYTES em UTF-8 (o "ã" custa 2) e o buffer
`s_state` era de 48. O GCC 14 do IDF 5.5 trata isso como ERRO; o GCC 12
do host só avisa — e só quando o `set_state()` é inlinado na função que
passa a string, o que nem sempre acontece (no audio_uac.c o mesmo buffer
de 48 passava batido). Ou seja: nem o alvo nem o host garantiam a classe
inteira de bug.

`audio_i2s.c` também NÃO entra no syntaxcheck (precisa de driver/i2s_std.h
e esp_codec_dev.h reais). Esta checagem cobre os dois arquivos de forma
determinística, sem compilar nada: lê o tamanho declarado do buffer e o
comprimento em bytes de cada literal passado a `set_state()`.

Uso: python3 tools/hosttest/msg_budget.py
Saída: uma linha por arquivo + "MSG-BUDGET: OK" / "MSG-BUDGET: FALHOU".
"""

import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]

ALVOS = [
    "components/pda_audio/audio_i2s.c",
    "components/pda_audio/audio_uac.c",
]

# static char s_state[64] = "...";   → 64
RE_BUF = re.compile(r"\bchar\s+s_state\s*\[\s*(\d+)\s*\]")
# set_state("literal")  — pega o literal mesmo com aspas escapadas
RE_MSG = re.compile(r'\bset_state\(\s*"((?:[^"\\]|\\.)*)"')
# set_state(cond ? "a" : "b") — literais extras em expressões ternárias
RE_MSG_TERN = re.compile(r'\bset_state\([^;]*?;')


def tamanho_literal(s: str) -> int:
    """Bytes que o literal ocupa depois de processado pelo compilador.

    Conta escapes (\\n, \\", \\xNN) como o C faria e mede em UTF-8 — é a
    métrica que o -Wformat-truncation usa.
    """
    out = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            nxt = s[i + 1]
            if nxt == "x":                       # \xNN
                j = i + 2
                hexd = ""
                while j < len(s) and s[j] in "0123456789abcdefABCDEF":
                    hexd += s[j]
                    j += 1
                out.append(int(hexd or "0", 16) & 0xFF)
                i = j
                continue
            mapping = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, '"': 34, "'": 39}
            out.append(mapping.get(nxt, ord(nxt)))
            i += 2
            continue
        out += c.encode("utf-8")
        i += 1
    return len(out)


def checar(caminho: pathlib.Path) -> bool:
    src = caminho.read_text(encoding="utf-8")

    m = RE_BUF.search(src)
    if not m:
        print(f"  ??   {caminho}: não achei 'char s_state[N]'")
        return False
    n = int(m.group(1))
    util = n - 1                                  # 1 byte p/ o NUL

    msgs = []
    for lit in RE_MSG.findall(src):
        msgs.append(lit)
    # ternários: varre cada chamada inteira e coleta os literais dentro
    for call in RE_MSG_TERN.findall(src):
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', call):
            if lit not in msgs:
                msgs.append(lit)

    if not msgs:
        print(f"  ??   {caminho}: nenhum literal em set_state()")
        return False

    maior = max(tamanho_literal(x) for x in msgs)
    ok = True
    for lit in msgs:
        b = tamanho_literal(lit)
        if b > util:
            ok = False
            print(f"  FALHA {caminho.name}: \"{lit}\" = {b} bytes > {util} úteis")
    pior = max(msgs, key=tamanho_literal)
    status = "OK " if ok else "ESTOUROU"
    print(f"  {status} {caminho.name}: s_state[{n}] ({util} úteis), "
          f"{len(msgs)} mensagens, maior = {tamanho_literal(pior)} bytes "
          f"(\"{pior[:44]}{'...' if len(pior) > 44 else ''}\")")
    return ok


def main() -> int:
    print("--- rodando msg_budget (orçamento de s_state, v2.3.1) ---")
    tudo = True
    for rel in ALVOS:
        p = ROOT / rel
        if not p.exists():
            print(f"  ??   {rel} não existe")
            tudo = False
            continue
        tudo = checar(p) and tudo
    print("MSG-BUDGET: OK" if tudo else "MSG-BUDGET: FALHOU")
    return 0 if tudo else 1


if __name__ == "__main__":
    sys.exit(main())

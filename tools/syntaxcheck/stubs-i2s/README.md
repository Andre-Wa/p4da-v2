# stubs-i2s/ — headers de áudio (I2S + esp_codec_dev) para o syntaxcheck

## Por que existem

`components/pda_audio/audio_i2s.c` era o **único** arquivo da camada de áudio
que o `tools/syntaxcheck/run.sh` não conseguia compilar: ele inclui
`driver/i2s_std.h`, `esp_codec_dev.h`, `esp_codec_dev_defaults.h` e
`es8311_codec.h`, cuja cadeia real de headers (soc/, hal/, esp_private/) é
grande demais para vendorizar como foi feito em `real/usb/`.

O custo disso apareceu na v2.3.0: o build do alvo morreu em
`-Werror=format-truncation` justamente nesse arquivo (mensagem de 53 bytes
UTF-8 num `s_state[48]`), e nada no host tinha visto o código.

## O que são e o que NÃO são

Assinaturas **derivadas do uso** em `audio_i2s.c` — o mesmo critério já
adotado para `tools/syntaxcheck/stubs/usb/hid_host.h`. Consequências:

- **Pegam**: erro de sintaxe, identificador não declarado, variável não
  usada, ponteiro incompatível, `%s`/`%lu` errado no `ESP_LOGx` (o stub de
  `esp_log.h` roteia para `printf`, que tem atributo de formato), estouro de
  buffer que o GCC consiga provar.
- **NÃO pegam**: drift da API real do `esp_codec_dev`/`i2s_std` (se um
  protótipo mudar no componente gerenciado, este stub continua igual e o
  erro só aparece no alvo). A autoridade é o build do alvo; o stub serve
  para o erro de *forma*, não de *contrato*.

Só os campos realmente usados estão declarados — os demais ficam de fora de
propósito (designated initializers em C não exigem a struct completa), para
que o stub invente o mínimo possível.

## Lacuna que permanece

`-Wformat-truncation` depende de o GCC inlinar `set_state()` no chamador; o
GCC 14 do alvo inlina, o GCC 12 do host nem sempre. Para a classe específica
que quebrou a v2.3.0 (literal longo demais para o buffer de estado) a
garantia é `tools/hosttest/msg_budget.py`, que mede os bytes UTF-8 de cada
literal contra o `s_state[N]` declarado — determinístico, sem compilar.

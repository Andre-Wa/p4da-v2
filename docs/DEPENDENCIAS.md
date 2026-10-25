# Matriz de dependências — p4da-v2 L1 (ESP-IDF 5.5.x)

Política herdada da v1 e endurecida: **tudo pinado `==`**. Ranges `^`
deixam o resolve saltar de minor sem aviso — foi exatamente assim que
Lua 5.5 e Slint 1.18 entraram e quebraram o build nas primeiras rodadas
da v1. Ao atualizar qualquer item, atualize esta matriz e rode a suíte
de `docs/TESTES.md`.

O que já nos mordeu não foi incompatibilidade declarada, e sim mudança
de API dentro da mesma major. Nenhuma dependência abaixo declara
incompatibilidade com IDF 5.5.1 (auditoria da v1 contra o registry).

## Versões da fundação (build de referência: IDF v5.5.1, GCC 14.2)

| Componente | Versão | Onde | Status v1 |
|---|---|---|---|
| `slint/slint` | **==1.12.1** | main | ✅ provado (ver regressão 1.18 abaixo) |
| `espressif/esp_lcd_st7701` | **==1.1.5** | pda_board | ✅ (série 1.x; a 2.x pede IDF 6.0+) |
| `espressif/esp_lcd_touch_gt911` | **==1.2.1** | pda_board | ✅ |
| `espressif/esp_lcd_touch` | **==1.2.1** | pda_board | ✅ (transitiva, pinada p/ congelar) |
| `joltwallet/littlefs` | **==1.22.3** | pda_board | ✅ (formato on-disk estável na série 2.x) |
| `espressif/lua` | **==5.5.0** | pda_script | ⚠️ ABI 32-bit PRIVATE — ver abaixo |
| `espressif/usb_host_hid` | **==1.2.1** | pda_usb | ✅ |
| `espressif/esp_hosted` | **==2.12.9** | pda_net | ✅ (par c/ slave 2.12.9) |
| `espressif/esp_wifi_remote` | **>=1.3.1** | pda_net | ✅ |
| `espressif/esp_codec_dev` | **==1.6.2** | pda_audio | ✅ (ES8311) |
| `espressif/usb_host_uac` | **==1.5.0** | pda_audio | ⚠️ UAC1-only (#3) |

`idf: ">=5.3,<6.0"` em todos os yml: IDF 6.x recusa P4 rev <3.1 (chip
ES) e muda APIs junto (st7701 2.x etc.).

## Contratos de ABI/versão que NÃO se tocam sem medição

1. **`espressif/lua` 5.5 compila a VM com `LUA_32BITS=1` como define
   PRIVATE** (bug espressif/developer-portal #188). `pda_script` repete o
   define e `pda_config.c` tem `_Static_assert(sizeof(lua_Number)==4)`.
   Sem isso: `lua_tonumber` lido como double satura em INT_MAX no RISC-V
   (causa raiz do "veneno INT_MAX" do system.lua na v1).
2. **Slint 1.18.1 = regressão de flash**: o prebuilt embute glifos ~20x
   maiores (31,8 MiB de .rodata num app de ~3 MiB) e estoura os 16 MB.
   Revisitar só com mudança de IDF/fontes e medição de `.rodata`.
3. **`usb_host_uac` 1.5.0 é UAC1-only** (`uac_host.c:1527`): devices UAC2
   enumeram mas não viram speaker (QCY H3S da v1 era exceção UAC1). Entra
   na L3 com este aviso no yml.
4. **littlefs 1.14→1.22 direção única**: partições escritas pela versão
   nova podem não montar na antiga.

## O que entra em cada camada (volta ao build junto do componente)

| Camada | Dependências |
|---|---|
| L2 (v2.1.0, no build) | `espressif/esp_hosted ==2.12.9` + `espressif/esp_wifi_remote >=1.3.1` (rules esp32p4) em `pda_net`; linhas ESP_HOSTED_*/SNTP no sdkconfig |
| L3 (v2.2.0, no build) | `espressif/usb_host_uac ==1.5.0` + `espressif/esp_codec_dev ==1.6.2` em `pda_audio` |
| L4 | nenhuma nova (ADC/SAR são do IDF) |

## Warnings aceitos (documentados, não são bugs)

- `esp_lcd_touch_get_coordinates` deprecated: aviso vem de dentro do
  `slint-esp.cpp` (Slint chamando API antiga do esp_lcd_touch 1.2).
- Campos novos de `usb_host_config_t` no IDF 5.5: nosso init por
  agregado deixa-os zero (default correto); warning cosmético.

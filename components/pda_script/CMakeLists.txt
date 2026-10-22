# pda_script — VM Lua protegida + configurações do sistema.
#
# lua_runtime.c: Lua 5.5 (espressif/lua) com lua_pcall, hook anti-loop
# (20M instruções), allocator com teto (2 MB) e API pda.* p/ scripts do
# cartão. pda_config.c: struct C é a fonte da verdade; system.lua é a
# serialização legível (parse em estado Lua temporário + saneamento +
# sombra NVS).
idf_component_register(
    SRCS
        "lua_runtime.c"
        "pda_config.c"
    INCLUDE_DIRS "include"
    REQUIRES
        pda_board           # storage_init.h (raiz ativa, helpers de arquivo)
        nvs_flash           # sombra NVS da config
        esp_timer           # uptime/millis
        espressif__lua      # VM Lua 5.5
)

# ---------------------------------------------------------------------------
# ABI do espressif/lua 5.5.0 — OBRIGATÓRIO, não remover.
#
# O componente compila a VM com LUA_32BITS=1 (lua_Number=float 32 bits,
# lua_Integer=int 32 bits), mas esse define é PRIVATE no CMakeLists dele
# (idf-extra-components/lua/CMakeLists.txt: "target_compile_definitions(...
# PRIVATE LUA_32BITS=1)"; bug relatado em espressif/developer-portal
# discussion #188, comentário de 2026-07-24). Sem o define aqui, este
# componente compila contra os headers com lua_Number=double/lua_Integer=
# int64 e o link é feito contra uma VM float/int32:
#   * lua_tonumber devolve float nos 32 bits baixos de fa0; ler como double
#     dá lixo finito -> (int) satura em INT_MAX no RISC-V (fcvt.w.d). Foi a
#     causa raiz do "veneno INT_MAX" do system.lua na v1 (boots
#     2026-09-24..30) — o cartão SD era inocente.
#   * lua_tointeger "sobrevivia" por sorte: o psABI RISC-V garante retorno
#     int32 sign-extended em a0, e o caller lia int64 de a0.
#   * lua_pushnumber(double) do nosso lado era lido como float pela VM
#     (pda.settings.get de scripts devolvia lixo).
# O _Static_assert no topo de pda_config.c falha o build com mensagem clara
# se um dia este define sumir ou o componente mudar de ABI.
#
# Na v1 este define vivia no main/CMakeLists.txt (tudo era um componente
# só). Na v2 ele pertence a pda_script — ÚNICO componente que inclui
# lua.h. A camada app (main/) fala com a VM só via lua_runtime.h, que não
# expõe tipos do Lua; se um dia main incluir lua.h, o define precisa ir
# para lá também (e o static assert de pda_config.c não o protegerá).
# ---------------------------------------------------------------------------
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUA_32BITS=1)

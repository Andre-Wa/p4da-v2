-- logger_exemplo.lua — modelo de "script longo" da fundação (L1).
--
-- O padrão nasceu na v1 (v5.16.2, log_bateria.lua) e é o mesmo:
--   * pda.sleep_ms() suspende SÓ a task do script ("lua_script") — a UI
--     segue viva; o sleep não gasta o teto de 20M de instruções (o hook
--     conta instruções, não tempo), então um logger de minuto roda horas;
--   * cada linha é fechada na hora (fopen/fwrite/fclose por append): se a
--     bateria morrer no meio, tudo até a última linha completa já está
--     no cartão;
--   * rodar de novo CONTINUA o arquivo sem duplicar o header;
--   * falha de gravação (cartão cheio/removido) encerra graciosamente.
--
-- Este exemplo registra uptime em logs/tempo.csv ("n;uptime_s") a cada
-- INTERVALO_S até MAX_LINHAS — sem depender de bateria (camada L4).
-- Enquanto roda, nenhum OUTRO script inicia (serialização da VM; o
-- console avisa). Não teste com power.deep_sleep_after_s > 0.

local INTERVALO_S = 60     -- período de amostragem (s)
local MAX_LINHAS  = 8      -- limite do exemplo (edite à vontade)
local ARQ         = "logs/tempo.csv"

pda.fs.mkdir("logs")   -- já existir não é erro

local novo = not pda.fs.exists(ARQ)
if novo then
    pda.fs.write(ARQ, "n;uptime_s\n")
    pda.log(string.format(
        "logger_exemplo: gravando %s a cada %d s (máx. %d linhas)",
        ARQ, INTERVALO_S, MAX_LINHAS))
else
    pda.log("logger_exemplo: continuando " .. ARQ)
end

-- conta linhas existentes (desconta o header) p/ continuar sem duplicar
local n = 0
local atual = pda.fs.read(ARQ)
if atual then
    for _ in atual:gmatch("[^\n]+") do n = n + 1 end
    n = n - 1
    if n < 0 then n = 0 end
end

local novas = 0
while n < MAX_LINHAS do
    n = n + 1
    local linha = string.format("%d;%d\n", n, pda.uptime())
    if not pda.fs.append(ARQ, linha) then
        pda.log("logger_exemplo: FALHA ao gravar (cartão cheio/saiu?) — encerrando")
        n = n - 1
        break
    end
    novas = novas + 1
    pda.log(string.format("logger_exemplo[%d] %s", n, (linha:gsub("\n", ""))))
    if n < MAX_LINHAS then pda.sleep_ms(INTERVALO_S * 1000) end
end

pda.log(string.format("logger_exemplo: fim — %d linha(s)", novas))

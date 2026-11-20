# TouchOnVita v0.60 — compatibilidade e inicialização (20/11/2026)

## Relato
PS Vita Slim, Darkest Dungeon digital PCSE00919 update 1.17. START+L sem efeito, touch inoperante, UI parcial após recarregar HENkaku. O desenvolvedor testou com RePatch para classes. O save antigo não é um fator esperado: o plugin não lê os saves.

## Causas possíveis confirmadas no código v0.59
1. mode inicia em 0 se mode.ini está ausente/ilegível.
2. gui/focus hooks são instalados na inicialização apenas quando mode=1; alternar modo depois não os instala.
3. Assets RGBA são lidos apenas uma vez; falha na leitura deixa textos soltos sem o pergaminho.
4. Engine 1.17 só é reconhecido se estrutura de eboot corresponder às assinaturas/offsets testados. Update exibido no LiveArea não prova igualdade do executável, sobretudo com RePatch.
5. Se ux0:tai/config.txt estiver presente, pode prevalecer sobre ur0:tai/config.txt. Instalar linha só no arquivo inativo não carrega o plugin.
6. Não há dependência conhecida de RePatch nem do save. Uma alteração apenas dos assets do jogo não deve ser pré-requisito; se RePatch troca eboot a hipótese de diferença da assinatura precisa ser investigada.

## Correções experimentais v0.60
- Defaults funcionais na ausência de mode.ini; modo=0 explícito continua respeitado.
- Parser aceita BOM UTF-8, espaços e CRLF, com registro de campos inválidos.
- GUI e focus hooks preparados independentemente do estado mode=0/1 inicial, com pass-through quando desligado.
- Thread leve tenta recarregar somente sprites faltantes até cerca de 4s, sem acessos a disco no render por frame.
- Arquivo de diagnóstico de assets detalha presença, tamanho e status dos seis RGBA.
- Menu exibe NO GAME HOOK quando engine é incompatível e UI FILES MISSING quando os assets não estão prontos.
- A rota de clique nativo da v0.59 foi preservada.

## Testes realizados
- Build VitaSDK Docker: sucesso.
- Vita3K 1.16, mode.ini ausente: inicialização sem crash, engine_compatible=1, modo=1, hooks ativos.
- Vita3K 1.16, mode.ini BOM + CRLF e mode=0: desativação respeitada; hooks de GUI/foco continuam preparados.
- Vita3K 1.16, UI.rgba temporariamente indisponível e recolocado: recuperou o recurso, status complete=1, sem crash.
- NÃO validado em hardware real sem RePatch nem no executável digital do terceiro usuário.
- v0.59 pública e Vita físico do desenvolvedor não foram substituídos.

## Pacote de teste local
output/v0.60_bootstrap_compat_diagnostic/DDTouchUI-v0.60-DIAGNOSTIC.zip
Contém SUPRX, seis RGBA, mode.ini e instruções, sem áudio do jogo, saves, PSARC ou eboot.

## Informações necessárias do usuário afetado
- Verificar se existe ux0:tai/config.txt. Conferir seção *PCSE00919 do arquivo taiHEN ATIVO, contendo ur0:tai/DDTouchUI.suprx.
- Confirmar seis arquivos .rgba e mode.ini diretamente em ux0:data/DDTouchUI/ (não em subpasta duplicada).
- Confirmar modo ativo: mode=1, fast_input=1, spatial_only=1, menu_click=1, widget_probe=1, direct_click=1, direct_all_widgets=1.
- Recarregar configuração HENkaku e reabrir jogo inteiramente. Não segurar L ao iniciar o aplicativo; alguns sistemas ignoram plugins nessa situação.
- Arquivos para enviar: ux0:data/DDTouchUI/hooks.txt, engine_hook_init.txt, engine_compat_probe_v047.txt, mode.ini.
- Com v0.60, incluir startup_v060.txt e assets_status_v060.txt.
- Se engine_compatible=0, não forçar hooks ou alterar o executável até examinar os relatórios.
- Testar conflito com plugins de terceiros apenas após backup da configuração, sem remover entradas essenciais de kernel.

## Fontes
https://github.com/yifanlu/taiHEN
https://cimmerian.gitbook.io/vita-troubleshooting-guide/plugins-related-problem/plugins-not-loading
https://consolemods.org/wiki/Vita:Applying_ROM_Patches

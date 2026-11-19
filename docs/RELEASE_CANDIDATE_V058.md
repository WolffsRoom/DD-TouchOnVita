# v0.58 — Candidata à primeira release

**Criado em:** 18/11/2026
**Estado:** documentação congelada nesta versão; distribuição pública ainda não autorizada automaticamente.
**Nome sugerido:** Darkest Dungeon — Touch UI Vita v0.58 (Experimental)
**Jogo:** PCSE00919 (Vita update 1.17).

## Proposta para primeira publicação

Uma release de teste pública que apresente o menu customizado e interação por toque sem prometer suporte total a todos os elementos. Não usar o número v1.0 antes de concluir a matriz de validação.

**Recursos presentes**
- Painel UI MOD personalizado, indicador Selector/On/Off/Mod On/Mod Off.
- Start+L para abrir/fechar; fade-out rápido; navegação por touch e botões.
- Foco touch persistente; handoff para gamepad.
- Rota v0.58 de clique nativo por toque único — experimental, sem X global.
- Sons opcionais com ganho 32% e fade, desde que obteníveis legalmente.

**Estado conhecido**
- Build Docker/VitaSDK concluída.
- Vita3K iniciou com hooks ativos sem crash (boot/smoke test).
- v0.58 transferida para Vita físico e leitura SHA256 independente confirmou instalação.
- Feedback positivo do autor: “Ficou bom!” e sinalização de interesse em primeira release.
- Ainda não há evidência registrada de todas as ações corretas em campanha, opções, cidade, combate, saves e áudio. Não declarar compatibilidade 100%.

## Checklist antes de publicar

- [ ] Testar menu inicial: um toque em Campaign e demais botões; nada ao tocar no espaço vazio.
- [ ] Testar três saves; cada slot deve abrir o slot correspondente, sem voltar ao primeiro.
- [ ] Testar Options: tocar opção inferior e confirmar que ativou ela, não “Voltar ao jogo”.
- [ ] Testar inventário, heróis e trinkets: toque, seleção e fechamento.
- [ ] Testar habilidades de batalha (primeira, terceira e quarta) e alvos diferentes; NÃO executar ataque no inimigo errado.
- [ ] Testar toque fora do alvo: não deve provocar ação errada.
- [ ] Testar touch traseiro, controle após toque e controles físicos completos.
- [ ] Testar menu Start+L, configurações e reinicialização; verificar persistência do mode.ini.
- [ ] Confirmar efeitos sem pipocos e trilha/cutscene intactas com e sem abrir menu.
- [ ] Medir FPS em cidade e combate e verificar consumo de memória; não repetir regressão de ~20 FPS.
- [ ] Testar entrar/sair de menus, pausa e reiniciar jogo diversas vezes sem crash.
- [ ] Gerar pacote limpo com .suprx e recursos gráficos autorizados.
- [ ] Definir modo de reprodução de áudio autorizado para distribuição; excluir PCM/WAV extraídos de cópia do jogo.
- [ ] Adicionar créditos à arte e ferramentas e verificar licença de distribuição.
- [ ] Publicar release notes e instruções de rollback; indicar experimental se houver testes pendentes.

## Logs para validação

Coletar após teste em Vita: ux0:data/DDTouchUI/mode.ini, engine_hook_init.txt, native_widget_probe_v047.txt, native_click_v058.txt e engine_hook_status.txt. Registrar ações realizadas e resultado observado. A contagem de allowed não equivale a confirmar que o jogo executou o alvo correto.

## Pacote público: conteúdo e exclusões

**Pode incluir, conforme titularidade:** DDTouchUI.suprx, artes próprias da UI (PNG/RGBA), README, instruções de instalação e configuração de exemplo.

**Não incluir automaticamente:** eboot do jogo, PSARC, saves, bancos de áudio, WAV/PCM extraídos de Darkest Dungeon sem permissão. Os sons encontrados em Assets/UI MOD/Sounds são ativos de pesquisa local.

## Compilação, backup e rollback

Código principal: src/plugin.c.
Fonte preservada da etapa anterior: src/plugin_v057_before_native_click_v058.c.bak.
Binário da candidata: output/v0.58_native_direct_click/DDTouchUI.suprx.
Backup anterior à instalação: diagnostics/psvita_2026-11-18/v058_native_direct_deploy/.
Rollback: substituir .suprx antigo em ur0:tai e restaurar mode.ini correspondente, sem alterar saves ou config.txt. Reiniciar o jogo por completo.

## Proposta de mensagem curta de anúncio (quando validada)

Darkest Dungeon Touch UI is an experimental PS Vita plugin that adds native front-touch interaction, a themed in-game settings panel and optional UI sounds while preserving physical controls. This early release supports the validated PCSE00919 build; some gameplay interactions may still need refinement.

---
A primeira release será considerada pronta quando os casos prioritários acima forem confirmados em hardware. Até lá, v0.58 é um marco de desenvolvimento.

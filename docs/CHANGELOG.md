# Histórico de versões — Darkest Dungeon Touch UI Vita

Consolidação até **v0.58, 18/11/2026**. Histórico de desenvolvimento, não uma promessa de funcionalidade total em todas as versões. As referências de testes antigos continuam preservadas na pasta docs/ e em diagnostics/.

## v0.59 — Faster renderer and smoother UI sounds (18/11/2026)
- Packed RGBA alpha blend instead of per-pixel integer division; preloaded artwork and both status banners.
- 3-frame fade-in and 2-frame fade-out, pending on-device confirmation.
- Trimmed/attenuated UI sounds, crossfaded changes and reused a temporary MAIN audio port without BGM reservation.
- Disabled runtime_logs in physical test config to avoid periodic file writes.
- Compiled and smoke-tested with Vita3K, installed to physical Vita via FTP and independently hash-verified, backup made.
- Kept the v0.58 native one-touch activation logic unchanged.
- See docs/V059_RENDER_AUDIO_OPTIMIZATION_20261118.md.

## v0.58 — Single-tap native widget activation (18/11/2026)
- A nova rota direct_click tenta ativar diretamente o widget que recebeu hover do motor, uma vez por gesto, sem simular X global.
- direct_all_widgets expande além dos menus iniciais, para widgets da GUI com IDs até 128 (inclusive IDs de skills vistos em logs).
- Otimização de áudio: volume dos efeitos extras reduzido a 32%, fade curto nas bordas e bloco de silêncio ao liberar a porta.
- Builds VitaSDK e inicialização Vita3K realizadas. Binário instalado no PS Vita via FTP e hash confirmado em segunda conexão.
- Feedback do usuário: “Ficou bom! Talvez essa seja a primeira release do projeto.”
- Pendente: testes formais de combate/alvos, todos menus, saves, áudio sem distorção, FPS e regressões de foco.

## v0.57 — Audio BGM port fix (17/11/2026)
- Corrigida a reserva contínua da porta BGM feita pela versão inicial de UI sounds. A porta MAIN passou a ser aberta somente para um efeito curto e liberada após a reprodução.
- Teste de boot no Vita3K sem crash; instalação física verificada por FTP.
- Persistia relato de áudio pipocado ao abrir o menu. Corrigido experimentalmente na v0.58.

## v0.56 — Single-tap spatial candidate (17/11/2026, somente protótipo)
- Primeiro experimento de reconhecer o mesmo widget no mesmo gesto e autorizar clique sem segundo toque.
- Compilado, iniciado em Vita3K, mas o teste passou pela longa introdução do jogo e não demonstrou clique em menu.
- Não instalado no Vita físico; código de pesquisa preservado.

## v0.55 — Config persistence (16–17/11/2026)
- Corrigida persistência de repeat_confirm e widget_probe quando configurações são salvas pelo painel.
- Instalado por FTP; diagnóstico experimental de confirmação mantido.

## v0.54 — Two taps on same frontend widget (16–17/11/2026)
- Tentativa conservadora de reconhecer dois toques no mesmo widget/contexto, ID 1–16 e cenas iniciais conhecidas.
- Cenários fora da whitelist não ativavam, para evitar ataques por engano.
- Em hardware físico, logs mostraram confirmações autorizadas, mas usuário relatou que clicar não funcionava.
- Não resolveu o requisito de um toque = um clique.

## v0.53 — Fast fade-out (16/11/2026)
- Fade-out do menu alterado de ~13 para 3 frames (incremento alfa negativo de -20 para -85).
- Sons e UI mantidos. Instalação física confirmada.

## v0.52 — UI MOD and game-sourced UI sounds (16/11/2026)
- Redesenho usando Assets/UI MOD: UI, Selector, On, Off, Mod On e Mod Off.
- Menu alterado para Start + L, menu de opções pré-renderizado na arte, fade de entrada e saída.
- Três sons obtidos de ui_shared.bank e convertidos em PCM para tocar no Vita (open/select/confirm).
- Primeiro sistema de áudio abriu porta BGM permanente, posteriormente corrigida.
- Instalado no Vita após testes de inicialização.

## v0.51 — Mode status artwork (16/11/2026)
- Imagens Mod On/Mod Off e indicação de modo ao iniciar e ao alternar a configuração.
- Instalada em Vita e validada por hash.

## v0.50 — Custom artwork first integration (15–16/11/2026)
- Integração de Assets/UI.png como RGBA no overlay; controles centralizados na área do pergaminho.
- Preparação inicial de caminho ux0:data/DDTouchUI/UI.rgba.
- Compilação concluída, teste visual físico dependia de feedback.

## v0.49 — Gesture research (15/11/2026)
- Duplo toque para X restrito ao frontend; dois dedos para círculo; arrastar mapa somente para diagnóstico, não movimento efetivo.
- Não houve validação completa. Código experimental preservado em output/v0.49_red_ui_gestures_experimental/.

## v0.48 — Red UI (15/11/2026)
- Paleta de painel vermelho/vinho e novos elementos visuais, antes da arte personalizada da UI MOD.

## v0.47 — Persistent focus (15/11/2026)
- Ao soltar o dedo, a seleção visual permanece. Controle físico retoma a prioridade no próximo input.
- Usuário confirmou a persistência funcional no Vita físico; o clique ainda não foi resolvido.
- Referência: docs/DDTOUCH_V047_PERSISTENT_FOCUS_20261115.md.

## v0.44–v0.46 — Native GUI hook isolation / crash hardening (14–15/11/2026)
- Estudos do dispatcher GUI, hover real de widgets e comparador de foco.
- v0.44 identificou e ativou Campaign no Vita3K, com limitações em slots e risco de crash no hardware.
- Crash em build física motivou rollback e testes de isolamento de hooks.
- Mantida proteção contra leitura prematura de ponteiros GUI em startup.
- Ver docs/DDTOUCH_V044_FRONTEND_CLICK_20261114.md e docs/DDTOUCH_V044_PREMENU_CRASH_20261115.md.

## v0.39–v0.42 — Fast native pointer and FPS
- Abandono de parte dos hooks pesados no modo fast_input; bloqueio de Cross genérico que executava habilidade incorreta.
- Melhoria esperada em FPS ao evitar logging síncrono e injeções indevidas; sempre testar no hardware.
- v0.42 focou em estabilidade.

## v0.31–v0.38 — Vita 1.17 compatibility and stability (07–08/11/2026)
- Descoberta da diferença de +0xC0 no layout de dados e assinaturas distintas do eboot.
- Dual layout para Vita 1.16/1.17; configuração de taiHEN e diagnósticos por FTP.
- Otimização removendo log de múltiplos MB gerado em callbacks de interface.

## v0.12–v0.30 — Early research (06–07/11/2026)
- Estudos dos eventos reais do engine, mouse state, coordinate conversion, hit-test, frontend GUI router e slots.
- v0.16/v0.26/v0.30 registraram em cenários controlados abertura do Campaign por toque e identificação de widgets.
- Persistiam incompatibilidades entre hover, clique de elementos, confirmação de campanhas e ações de combate.
- Todos os protótipos antigos estão documentados individualmente em docs/.

---

**Estado consolidado:** v0.58 candidata à primeira publicação, ainda não versão v1.0 estável. Não publicar áudio extraído do jogo sem autorização.

# Documentação técnica — DD Touch UI PS Vita (v0.58)

**Consolidação:** 18/11/2026. Jogo PCSE00919, PS Vita update 1.17; Vita3K isolado update 1.16.
**Projeto:** C:\Users\wolff\Documents\SDKVita\Darkest Dungeon Touch UI Vita.
**Código:** src/plugin.c em C, CMakeLists.txt, DDTouchUI.yml.
**Build física instalada:** output/v0.58_native_direct_click/DDTouchUI.suprx.
**Hash SHA-256:** 819811a9baae70d8db27b6b4ce1bb12241aaa268298709c1429999bf90bda541.

## Objetivo e arquitetura

A tela frontal deve permitir selecionar **e ativar** o widget sob o dedo, sem confirmar o item anteriormente focado pelo controle. Preserve D-Pad, botões, touch traseiro e os saves.

Plugin taiHEN em modo usuário; hooks nos imports SceTouch (Read/Peek), SceDisplaySetFrameBuf, SceCtrl e, quando a versão do eboot é reconhecida, no mouse nativo/dispatcher GUI. O touch frontal em coordenadas ~1920x1088 é convertido para framebuffer 960x544. As posições das variáveis da engine diferem entre 1.16/1.17 (+0xC0 na 1.17); o inicializador valida assinaturas e segmentos antes de aplicar hooks internos.

O dispatcher GUI em código offset 0x3710C detecta widgets. O comparador de foco (0x49F66) evita conflito com seleção anterior do D-Pad, mantendo hover após soltar o dedo até o próximo uso do controle físico. As primeiras builds com X sintético executavam o ataque errado e chegaram a causar queda para ~20 FPS. Por isso fast_input=1 e spatial_only=1 evitam cliques globais e I/O intenso em hooks.

### Evolução de confirmação

v0.54/v0.55: segundo toque limitado a contextos iniciais strt, sgsb, sgsc, sgsd e IDs <=16; logs físicos registraram first=14 success=9, mas o usuário ainda não conseguia clicar. Logs posteriores registraram widgets de combate ID 0x1a–0x1f nos contextos 736b6c6c / 736b6c6e, não cobertos anteriormente.

v0.58: em um toque, o dispatcher confirma hover no widget (event[0]) e o plugin marca experimentalmente o evento de ativação do próprio widget (event[1]), uma vez por gesto; não envia X global. direct_click=1 e direct_all_widgets=1 habilitam a nova rota; IDs reconhecidos até 128. Resultado relatado pelo usuário: a melhoria ficou boa. **Ainda não há validação formal de que todos os ataques atinjam o alvo correto ou que todos os menus/saves funcionem.**

## Overlay UI MOD

Atalho **Start + L**; D-Pad vertical navega, X seleciona, círculo fecha. Opções: Touch, Logs, Input (somente leitura), Pointer (reset). O painel também é navegável por toque. O item Touch requer acionamento no indicador direito, para evitar desligamento acidental.

Fontes: Assets/UI MOD/ — não redesenhar os títulos porque UI.png já os contém.

| PNG | Arquivo de dados no Vita | Dimensão |
|---|---|---|
| UI.png | UI.rgba | 430x513 (crop 267,0–697,513) |
| Selector.png | Selector.rgba | 249x42 |
| On.png | On.rgba | 67x53 |
| Off.png | Off.rgba | 67x53 |
| Mod On.png | Mod_on.rgba | 291x125 |
| Mod Off.png | Mod_off.rgba | 291x125 |

Recursos são RGBA8888 **crus**, não PNG renomeado, em ux0:data/DDTouchUI/. Alpha blend é realizado por software sobre SCE_DISPLAY_PIXELFORMAT_A8B8G8R8. Fade-in: +24 alpha/frame; fade-out: -85 alpha/frame (3 frames, aproximadamente 50 ms a 60 FPS).

## Áudio

Sons em Assets/UI MOD/Sounds: open.wav/pcm, select.wav/pcm, confirm.wav/pcm, extraídos localmente da bank audio/secondary_banks/ui_shared.bank de um exemplar do jogo (ui_shr_window_popup, ui_shr_button_mouse_over, ui_shr_button_click). PCM bruto mono, 48 kHz, signed 16-bit. Instalar em ux0:data/DDTouchUI/Sounds/.

v0.52 reservava porta BGM e houve relato de música de cutscene ausente; v0.57 passou para MAIN temporário, liberado após tocar. Usuário relatou pipocos; v0.58 atenua PCM para 32%, aplica fade ~10 ms e envia um bloco de silêncio ao encerrar. **Correção de som físico da v0.58 ainda não confirmada**.

Esses samples não devem ser redistribuídos sem autorização dos titulares.

## mode.ini

Exemplo de configuração de teste v0.58 em ux0:data/DDTouchUI/mode.ini:

    mode=1
    doubletap=0
    hero_touch=1
    map_touch=1
    outside_close=1
    focus_hold=1
    fast_input=1
    runtime_logs=1
    spatial_only=1
    hover_persist=1
    menu_click=1
    repeat_confirm=0
    widget_probe=1
    ui_sounds=1
    direct_click=1
    direct_all_widgets=1

mode=0 desliga a camada touch. hover_persist=1 preserva seleção. spatial_only=1 bloqueia clique genérico. menu_click=1 instala hooks GUI. repeat_confirm=0 desliga duplo toque. direct_click=1 ativa novo evento de clique no widget; direct_all_widgets=1 amplia os contextos. ui_sounds=0 desativa os sons extras. runtime_logs=1 ativa diagnóstico e pode ser 0 após testes.

## Build / instalação / rollback

VitaSDK + CMake, build com os comandos 'cmake -S . -B build' e 'cmake --build build -j4'. Em Windows com Docker, montar a pasta do projeto em /work usando imagem vitasdk/vitasdk:latest. Saída: build/DDTouchUI.suprx.

Instalar em ur0:tai/DDTouchUI.suprx, dentro da seção *PCSE00919 do ur0:tai/config.txt. Copiar RGBA e PCM conforme acima. Fechar completamente o jogo após trocar plugin. v0.58 instalada via FTP no Vita com backup de v0.57 em diagnostics/psvita_2026-11-18/v058_native_direct_deploy; upload e leitura por SHA-256 coincidiram. Configuração alterada explicitamente para o novo teste; saves e config.txt preservados.

Para rollback: restaurar a SUPRX anterior e o mode.ini do backup. Para desabilitar primeiro o touch sem substituir arquivos: mode=0. Se ocorrer confirmação de alvo errado ou crash, interromper testes.

## Logs

Arquivos em ux0:data/DDTouchUI/:

| Log | Conteúdo |
|---|---|
| engine_hook_init.txt | Engine 116/117, assinatura/layout, hooks disponíveis |
| engine_hook_status.txt | down/up, foco, handoff e eventos bloqueados |
| native_widget_probe_v047.txt | IDs, flags, posições e referências de cena |
| native_click_v058.txt | attempts, allowed, denied de eventos de ativação |
| mode.ini | Flags realmente usadas |
| gui_widget_dispatch_probe.txt | Legado; pode registrar 0 com o hook rápido ativo |
| sound_status_v052.txt / sound_status_v057.txt | Diagnósticos antigos, não necessariamente da execução atual |

Contador allowed NÃO é prova de que o jogo executou a ação correta. Confirmar cena real, binário ativo e tempo do log.

## Limitações / próximos testes

Validar menu inicial, três slots de save, menu Options e retorno pelo X, cidade, heróis, inventário, mapa, scroll/drag, batalha (habilidade e alvo individualmente), touch traseiro, controle após touch e FPS. Confirmar música de cutscene, música de fundo e ausência de pipocos ao abrir/fechar UI. Regressões de controles e ataques incorretos são críticas. Não anunciar total compatibilidade nem chamar de v1.0 estável sem matriz de testes.

Mais detalhes nos relatórios históricos em docs/ e backups em diagnostics/.

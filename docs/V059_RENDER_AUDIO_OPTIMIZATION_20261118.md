# v0.59 — UI performance e efeitos sonoros (18/11/2026)

**Alvo:** Darkest Dungeon PCSE00919 update 1.17, Vita físico.
**Binário:** output/v0.59_ui_fps_audio/DDTouchUI.suprx
**SHA-256:** 1738f6cbdc20b445cf0d00bf5add6ccd8c2a53482e10592489cc82fe90409e4a
**Estado:** instalado no Vita via FTP e confirmado com nova leitura. Os resultados visuais e sonoros em hardware ainda precisam de confirmação.

## Problemas relatados
- Na v0.58, clique com um toque funcionava bem, mas a abertura de UI diminuía o FPS.
- O fade-out ficava entrecortado, e os sons da interface pipocavam.
- A confirmação nativa de widgets NÃO deveria ser alterada.

## Render
- ui_blit mudou para RGBA8888 empacotado (32 bits), sem as três divisões inteiras por pixel semitransparente da implementação anterior.
- Pré-carrega sprites UI MOD e ambos os banners ON/OFF durante module_start, evitando I/O no primeiro frame do menu e ao alterar modo.
- Fade-in acelera +85 alfa/frame (3 frames). Fade-out reduz 128 alfa/frame (2 frames). Duração em milissegundos varia com FPS real.
- Teste numérico por 60.000 pixels aleatórios: diferença máxima de 3/255 nos canais comparado à composição anterior.
- Não modifica a lógica v0.58 de native_click/event[1].

## Áudio
- PCM originais extraídos da cópia do jogo continuam externos: open, select, confirm.
- Encurta a duração reproduzida dos sons para aproximadamente 0,60s, 0,30s e 0,40s, retirando caudas quase silenciosas.
- Reduz volume dos efeitos para 27% e aplica envelope de entrada (10ms) e saída (40ms).
- Troca de efeito com crossfade de ~5ms, evitando corte abrupto.
- Porta MAIN de 512 frames reutilizada enquanto o menu está aberto; fechamento 0,6s após o fim do som e com o menu fechado. A porta BGM permanece livre.
- Prioridade da thread de áudio 0x10000140, preservando mais tempo para render e lógica.
- Validar áudio físico, especialmente cutscenes e música. Se houver regressão, testar com ui_sounds=0 para isolamento.

## Logs e desempenho
- A instalação física passou a runtime_logs=0 para evitar escrita periódica em ux0; o usuário pode reativar pelo painel.
- Os flags mode=1, direct_click=1, direct_all_widgets=1 e ui_sounds=1 foram mantidos.

## Evidências e backup
- Build VitaSDK Docker: sucesso, warnings antigos.
- Vita3K isolado update 1.16: carregou plugin e GUI/focus hooks, executou 20s sem crash; os recursos UI.rgba, Selector.rgba, Mod_on.rgba e Mod_off.rgba foram carregados.
- PS Vita: upload e readback independente confirmaram o SHA-256.
- Backup anterior da v0.58 e mode.ini em diagnostics/psvita_2026-11-18/v059_ui_fps_audio.
- Não modifica eboot, PSARC, saves ou outros plugins.

## Teste físico necessário
1. Fechar/reabrir completamente o jogo.
2. Abrir e fechar Start+L em menus, cidade e combate; comparar fluidez de fade e FPS com a v0.58.
3. Navegar entre opções repetidamente ouvindo open/select/confirm, conferindo ausência de estalos.
4. Reproduzir cutscene/música de fundo ao abrir/fechar UI.
5. Conferir que um toque continua ativando o widget correto e gamepad permanece funcional.

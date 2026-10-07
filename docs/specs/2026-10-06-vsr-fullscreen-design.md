# VSR apenas em tela cheia e transições

Pedido: usar o efeito apenas no player em tela cheia e reduzir o atraso observado
ao entrar/sair. Autorização de implementação local e testes já fornecida pelo
usuário; manter perfil e abas. Não alterar DRM, níveis nem processamento por FPS.

O Blink já identifica vídeo ou ancestral DOM em fullscreen, inclusive controles
customizados e iframe, por SetIsEffectivelyFullscreen. Propagar esse estado ao
VideoFrameCompositor por uma época atômica (bit baixo indica ativo). Expor consulta
virtual com padrão0 no VideoRendererSink. É por player; F11 isolado e modo teatro
não contam como fullscreen do player, nem PiP.

Renderer consulta a época na admissão, apresentação e callback de conclusão.
Cada mudança observada invalida o cache/geração; resultado antigo não pode voltar
ao sair/entrar rapidamente. Fora da tela cheia não submeter frames nem apresentar
saída aprimorada. Trabalhos já enviados drenam sem bloquear a UI ou reutilizar
texturas antes dos fences. Conservar serviço/modelo entre alternâncias para evitar
recarregamento a cada entrada; isso não promete liberar toda VRAM em modo janela.

Testar estado inicial em janela, entrada, saída, reentrada e conclusão atrasada;
verificar YouTube/HTML nativo/player em container/iframe e ausência de inferência
em janela. Medir DOM fullscreenchange e primeiras animações com efeito ligado e
desligado. O teste inicial já mostrou aproximadamente2s com efeito desligado,
portanto não atribuir toda demora ao VSR; qualquer otimização adicional deve ter
causa e comparação medidas. Compilar todas as unidades afetadas pelo novo método
virtual e pelo tamanho do compositor, não apenas o renderer.

A revisão encontrou saída pausada: nesse estado não há callback Render ativo.
Por isso o compositor notifica mudanças por callback que apenas posta no media
runner. O renderer guarda um único original apresentado durante fullscreen e
repinta-o ao sair, fora do lock, inclusive pausado. A notificação usa WeakPtr;
o callback é substituído na reinitialização. Flush/configuração liberam o
original. Registro de IDs só existe enquanto fullscreen, limpo na transição.

A restauração roda na sequência do compositor e só substitui o ID exato do
frame aprimorado antigo. Não rejeitar a restauração por época mais recente:
uma reentrada enquanto o repaint está enfileirado não pode descartar o único
original de um vídeo pausado. O ID impede sobrescrever um frame mais novo.


Atualização durante 1440p: os primeiros probes longos do YouTube não tiveram
continuidade e suas medições de GPU foram descartadas. O trace posterior dos
writers confirmou entrada DOM em 13.061s e saída real em 14.224s; portanto não
atribuir essa saída automaticamente à heurística de interseção. A causa da saída
nesse teste automatizado do YouTube permanece não determinada. O gate usa agora
HTMLVideoElement::GetDisplayType com elemento DOM fullscreen ou ancestral contendo
o vídeo (PiP mantém precedência), propagado por OnDisplayTypeChanged;
ExitedFullscreen desliga imediatamente. O teste local 1080p30→1440p manteve
fullscreen nos 40 samples durante 20s e passou o validador de continuidade.

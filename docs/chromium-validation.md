# Validação local do Chromium NVIDIA VSR

O navegador completo e a reprodução HTML5 real passaram localmente em 2026-10-06,
com VA-API NV12, EGL nativo Wayland, RTX 4070 SUPER, SDK 1.3.0.0. Veja a correção sobre sandbox abaixo.
Os relatórios detalhados ficam fora do Git em
`/home/caduhd4/Builds/brave/validation/`.

## Ambiente local

```bash
export NVVFX_BROWSER_BUILD_ROOT=/home/caduhd4/Builds/brave
export VFXSDK_ROOT=/home/caduhd4/Downloads/linux-nvidia/.sdk/VideoFX
```

Execute a partir do worktree do projeto. O Chromium fica em
`$NVVFX_BROWSER_BUILD_ROOT/src`; o SDK é externo e usa headers oficiais.
`tools/chromium/build.sh` mantém a reserva de 20 GiB e usa oito jobs por padrão.
Com argumentos, compila somente os alvos pedidos; sem argumentos, compila
`chrome`.

## Core e testes Chromium

A suíte standalone atual passou 12/12, incluindo falha de sincronização CUDA no
encerramento da ponte GL. Os testes Python passaram 13/13, incluindo o gate de evidência de seleção VSR. O executável Chromium
`media_unittests` foi compilado; a seleção de 59 casos passou
após correções de IPC, lock e fixtures. A nova rodada standalone, com
primeiro frame e padrões de cor alternados, também passou 12/12.

O cliente encapsula a saída opaca como SharedImage `RGBX_8888` e
VideoFrame `PIXEL_FORMAT_XBGR`, com formatos canônicos coerentes. O teste IPC
de conclusão verifica opacidade, tamanho natural, timestamps, cor e lease/fence;
o teste nativo também passou com esse formato de saída. As texturas privadas
CUDA/VFX permanecem RGBA8.

O benchmark standalone repetido com 10.000 frames por modo registrou:

| Modo | p50 | p95 | p99 |
|---|---:|---:|---:|
| 4 (VSR_ULTRA) | 2,76070 ms | 3,02285 ms | 3,12218 ms |
| 11 (DENOISE_ULTRA) | 9,62662 ms | 10,1137 ms | 10,3188 ms |

Esses números medem somente o processamento GPU do core; não estimam latência
nem taxa de quadros do navegador.

```bash
tools/chromium/build.sh media_unittests
cd "$NVVFX_BROWSER_BUILD_ROOT/src"
out/Vsr/media_unittests \
  --gtest_filter='NvidiaVsr*:VsrClientIpcTest.*:VideoRendererImplTest.*' \
  --test-launcher-jobs=4 \
  --test-launcher-summary-output="$NVVFX_BROWSER_BUILD_ROOT/vsr-media-test-summary.json"
```

O filtro inclui explicitamente os testes IPC, cujo nome começa com
`VsrClientIpcTest`, além das regressões existentes do renderer. Compilação e
testes de mocks não comprovam processamento no navegador.

## SharedImages e caminho GPU-only

Em 2026-10-06, a regressão GPU passou 25/25 casos de compound backing e
`GpuChannel`/`GpuChannelManager`, incluindo bypass de memória compartilhada sem
alocação, backing Ozone exclusivamente CPU e bloqueio de cópia quando o conteúdo
fica desatualizado.

```bash
tools/chromium/build.sh gpu_unittests gl_tests
cd "$NVVFX_BROWSER_BUILD_ROOT/src"
out/Vsr/gpu_unittests \
  --gtest_filter='SharedContextStateTest.*:CompoundImageBacking*.*:GpuChannelTest.*:GpuChannelExitForContextLostTest.*:GpuChannelManagerTest.*' \
  --test-launcher-jobs=4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  EGL_PLATFORM=x11 out/Vsr/gl_tests --ozone-platform=x11 --use-gl=egl \
  --use-cmd-decoder=validating --enable-features=NvidiaVsrNativeEgl \
  --disable-features=NvidiaVideoSuperResolution \
  --gtest_filter='SharedImageFactoryTest.*'
```

Na execução nativa, a factory teve 5 casos aprovados e 1 skip
(`InvalidWebGPUUsage`, porque `WEBGPU_SHARED_BUFFER` não é suportado nessa
plataforma). Os dois casos de integração do serviço GPU também passaram ao serem
executados explicitamente com `--gtest_also_run_disabled_tests`:

```bash
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  EGL_PLATFORM=x11 out/Vsr/gl_tests --ozone-platform=x11 --use-gl=egl \
  --use-cmd-decoder=validating \
  --enable-features=NvidiaVsrNativeEgl,NvidiaVideoSuperResolution \
  --gtest_filter='SharedImageFactoryTest.*:NvidiaVsrNativeIntegrationTest.*' \
  --gtest_also_run_disabled_tests --test-launcher-jobs=1 \
  --test-launcher-summary-output="$NVVFX_BROWSER_BUILD_ROOT/vsr-native-service-summary.json"
```

O fixture percorreu a chamada Mojo do serviço: owner Skia converteu para uma
textura privada, o worker CUDA/VFX processou, e o serviço publicou a saída em
SharedImage. Modos 4 e 11 produziram três resultados completos por modo, com
quadrantes de cores alternadas, recorte de um frame com padding e orientação
correta. O teste também altera scissor/window rectangles durante a inferência,
para verificar que o blit publicado cobre a saída. As SharedImages são RGBA
sintéticas; isso ainda não prova o caminho NV12 do decoder VAAPI. O outro caso
verifica bypass protegido/encrypted antes da consulta/import de recursos. O primeiro pedido de cada modo expirou
na fila antes dos três resultados completos seguintes. O teste usa leitura de
pixels para verificar a fixture; isso é diagnóstico de teste, não caminho de
playback. É um fixture de processo GPU sem sandbox Chromium, não uma reprodução
HTML5 e não valida o hook de sandbox em execução.

Uma regressão adicional passou 25/25 testes de contexto/canal e compound backing. A
suite de mídia continua em 56/56. O `CU_STREAM_NON_BLOCKING` causava saída preta
e atraso de um frame por não ordenar o trabalho interno do NGX; o stream VFX do processor agora
usa `CU_STREAM_DEFAULT`; a ponte mantém sincronização explícita. Os testes verificam também o primeiro frame e a
alternância de padrões de cor. No teardown, o owner mantém a propriedade das
referências GL; recursos de worker cuja conclusão não é segura ficam em
quarentena. Essa alteração compilou e o teste nativo não produziu DCHECK.
A conversão Skia verifica flush/submit antes de chamar o worker e mantém
fallback original na falha; alterações GL/Skia marcam o estado Chromium para
restauração. O blit neutraliza clipping do contexto físico.
Os logs de referência ficam em `$NVVFX_BROWSER_BUILD_ROOT/`:
`vsr-native-service-tests.log`, `vsr-gpu-regression-tests.log`,
`vsr-media-tests.log` e `validation/vsr-default-stream-10000.log`.

VSR usa representações que admitem apenas um backing GPU existente e atual,
dos tipos GLTexture/EGLImage/Ozone, sem alocação lazy nem sincronização entre
backings. O início do acesso verifica novamente o conteúdo sob lock e faz
bypass se uma cópia interna fosse necessária. Os acessos normais do Chromium
mantêm sua política anterior.

## Navegador e comparação

O modo VSR exige Wayland/EGL NVIDIA no host validado. O launcher usa perfil
isolado e mantém o sandbox do Chromium ativo:

```bash
export NVVFX_BROWSER_BUILD_ROOT=/home/caduhd4/Builds/brave
export VFXSDK_ROOT=/home/caduhd4/Downloads/linux-nvidia/.sdk/VideoFX
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/1000
export DISPLAY=:0

bash tools/chromium/generate-media.sh
bash tools/chromium/run.sh
```

Para usar o Chromium interativamente, sirva as mídias localmente em outro
terminal e abra `http://127.0.0.1:8000/`:

```bash
python3 -m http.server 8000 --bind 127.0.0.1 \
  --directory "$NVVFX_BROWSER_BUILD_ROOT/media"
```

O seletor da página permite testar H.264 720p/1080p, VP9 e AV1. Para baseline,
use `tools/chromium/run-baseline.sh`; `NvidiaVsrNativeEgl` mantém o backend EGL
e `NvidiaVideoSuperResolution` controla a inferência separadamente. A rota NV12
usada pelo decoder VA-API requer EGL nativo no Ozone Wayland neste driver;
ANGLE não fornece uma representação GL para o backing usado pelo decoder.

O checker automatizado controla o navegador por um pipe DevTools local, sem
porta de depuração nem dependências Python adicionais. Ele abre um perfil
isolado, captura informações GPU, eventos de mídia, frames e descartes, e
encerra sua própria instância ao terminar. `--require-vsr` exige uma mensagem
do renderer sobre frame aprimorado selecionado para apresentação; completions do
SDK sozinhos não satisfazem o gate. Essa seleção não comprova scanout físico:


```bash
python3 tools/chromium/playback-check.py --baseline
python3 tools/chromium/playback-check.py --require-vsr
python3 tools/chromium/playback-check.py --require-vsr --clip h264-720p-60.mp4
python3 tools/chromium/playback-check.py --require-vsr --clip h264-1080p-30.mp4
python3 tools/chromium/playback-check.py --require-vsr --clip vp9-720p-30.webm
python3 tools/chromium/playback-check.py --require-vsr --clip av1-720p-30.webm
python3 tools/chromium/playback-check.py --require-vsr --mse --seconds 20
```

Resultados ficam em `$NVVFX_BROWSER_BUILD_ROOT/validation/`, fora do Git. Confira
`report.json` e `browser.log`: decoder efetivo, backend NVIDIA, sandbox ativo,
mensagens de bypass, completions NVIDIA VSR e seleções aprimoradas do renderer. Avanço do vídeo por si só não
comprova VSR. O caso MSE usa AVC High nível 4.0 e muda de 720p para 1080p aos
12 segundos.

`--capture-frame` salva `frame.png` e a geometria do elemento de vídeo no
relatório para comparação visual e de cor. A captura é apenas diagnóstico;
não faz parte do processamento dos frames de playback.

Para gerar os padrões opcionais de cor, sem arquivos de mídia no Git:

```bash
python3 tools/chromium/generate-color-fixtures.py
python3 tools/chromium/playback-check.py --baseline --capture-frame \
  --clip h264-709-limited-720p-30.mp4
python3 tools/chromium/playback-check.py --require-vsr --capture-frame \
  --clip h264-709-limited-720p-30.mp4
```

O gerador usa uma grade 4×2 com midtones, cores saturadas e preto/branco.
As quatro variantes cobrem BT.709 limited/full, matriz SMPTE 170M limited
com primárias/transfer BT.709, e BT.709 limited em 1080p. Os arquivos H.264
High@4.0 e o JSON `color-fixtures-golden.json` ficam em `media/`. O ffprobe e
o roundtrip RGB por FFmpeg passaram, com erro máximo de 2, 3, 2 e 2 níveis
nas quatro variantes; isso valida os arquivos de teste, não o decoder GPU.

A rota NV12 com sampler externo pode converter YUV no driver EGL antes do
Skia. Os hints EGL de matriz/faixa são consultivos, e o baseline usa a mesma
rota. Ainda é necessário medir BT.709/BT.601 e FULL/limited com padrões
conhecidos; os testes RGBA não comprovam essa conversão.

## Evidência de playback local

Os comandos abaixo passaram no host original:

```bash
python3 tools/chromium/playback-check.py --require-vsr --capture-frame \
  --clip h264-720p-30.mp4 --seconds 12
python3 tools/chromium/playback-check.py --require-vsr --capture-frame \
  --clip h264-1080p-30.mp4 --seconds 8
```

O 720p apresentou 120 frames aprimorados em 12 s, com ~18,5 ms GPU/frame no
modo 4. O 1080p apresentou saída denoised no mesmo tamanho, ~12,8 ms GPU/frame
no modo 11. As quedas foram 5/352 frames no 720p e 4/232 no 1080p; os frames
iniciais do teste incluem o aquecimento/carregamento do SDK. Isso confirma
seleção pelo renderer para apresentação e avanço HTML5 com sandbox; não mede
scanout físico em cada refresh do monitor. O primeiro pedido pode ser descartado
enquanto o SDK carrega, mantendo baixa latência dos frames apresentados.

Ainda falta testar sistematicamente pause, seek, tela cheia, dois vídeos, troca
MSE/geração antiga, bypass protegido/HDR/formatos não suportados no navegador e
qualidade em conteúdo real. Concluir revisão e Chromium antes do porte Brave,
sem alterar partições.

## Correção de reprodução contínua — 2026-10-06

Esta rodada substitui a conclusão anterior baseada em uma única seleção inicial.
A reprodução prolongada desativava a sessão pelo watchdog de 100 ms. O stream
configurado no SDK agora é o legacy stream (`nullptr`); o stream próprio continua
`CU_STREAM_DEFAULT`. O limite do watchdog foi preservado e sua desativação agora
emite aviso. A causa física dos tempos elevados anteriores não foi determinada.

Verificação da versão final: Python 13/13; CTest standalone 12/12;
59 testes selecionados de mídia Chromium; integração nativa 2/2. Reprodução
local por 20 s em 720p e 1080p atingiu pelo menos 240 seleções melhoradas em cada
caso. O primeiro player do episódio fornecido, testado diretamente por cerca de
25 s, apresentou NV12 H.264 1920x1080 via VA-API, pelo menos 240 seleções
melhoradas, 519 frames decodificados e zero descartados na última amostra.
Relatório: `/home/caduhd4/Builds/brave/validation/external-player-1791321443920753355/report.json`.
Fixtures: `validation/vsr-1791321377535445902` e `validation/vsr-1791321409818843822`.

O gate `--require-vsr` agora exige pelo menos 120 seleções e rejeita aviso de
watchdog. Contadores são marcos acumulados: não provam melhoria de todos os
frames, continuidade até o último instante, cada geração MSE ou scanout físico.
Frames atrasados/ocupados continuam usando o original. Em 1080p nativo aplica-se
DENOISE_ULTRA, modo 11, strength 1.0; não aumenta a resolução. Não foi demonstrada
equivalência de qualidade/desempenho com Windows; Brave ainda não foi portado.

Correção da afirmação sobre sandbox: o launcher não passa flags para desativar
sandbox, mas `SystemInfo` após reprodução reporta GPU `sandboxed=false` nesta
máquina. Portanto sandbox GPU ativo não está validado. Pause/seek/MSE, múltiplos
vídeos e a matriz completa de fullscreen/cores ainda requerem testes prolongados.

## Queda AV1 e composição por software — 2026-10-06, 18:50 BRT

A sessão interativa do YouTube caiu três vezes com SIGSEGV (GPU PIDs 1051719,
1081368, 1081666) e passou a `--use-gl=disabled`, com renderers
`--disable-gpu-compositing`. Isso explica a navegação pesada observada nesse
estado. Não é build debug (`is_debug=false`); o build é component.

O core 1081666 localiza a chamada nula em `ScopedVABuffer::~ScopedVABuffer`,
chamado por `AV1VaapiVideoDecoderDelegate::SubmitDecode`. GDB confirmou tabela
`vaDestroyBuffer_ptr` nula em libmedia_mojo_services e tabela inicializada em
libcontent. O source_set libva_stubs duplicava tabelas privadas em DSOs.
A fixture AV1 reproduziu a queda com VSR desligado; o checker antigo aceitou a
recuperação por software. O checker agora rejeita qualquer queda GPU registrada
no intervalo, tanto baseline como VSR, e captura SystemInfo após playback.

`chromium/patches/vaapi-component-stubs.patch` dá ao build component uma biblioteca
compartilhada única para os stubs VA-API, com símbolos exportados. Builds sem
component mantêm o source_set. apply.py aplica/verifica o patch. GN format,
check do patch contra índice upstream e reaplicação do overlay passaram.

Após correção: baseline AV1 5 s sem queda; AV1 e VP9 com VSR por 20 s cada,
ambos com pelo menos 240 seleções melhoradas e composição GPU ativa. Relatórios:
`baseline-1791323267103417494`, `vsr-1791323278998619457`,
`vsr-1791323363329675609` sob o diretório local validation.
Python 15/15 e os 59 testes selecionados de mídia passaram.

YouTube real (vídeo s8cP1Vt5US8), perfil temporário separado: selecionado hd720,
AV1 NV12 via VA-API, ao menos 360 seleções melhoradas em aproximadamente 35 s;
última amostra com 812 frames decodificados, 10 descartados e 33,72 s de mídia.
Sem watchdog/queda e com composição GPU ativa. Últimos 180 intervalos de rAF da
página: mediana ~5,6 ms e máximo ~22,2 ms. Essa medição não mede latência da UI
Chrome nem equivale a comparação controlada antes/depois.
Relatório: `validation/youtube-probe-1791323309034285322/report.json`.

720p seleciona VSR_ULTRA 4, strength 1.0, saída 1920x1080 no código, conforme
teste de configuração. Não há conclusão de qualidade visual equivalente ao
Windows nem garantia de todos os frames melhorados; o scheduler conserva
fallback original. A impressão de qualidade antes da correção não pode ser
atribuída ao VSR ativo sem evidência por frame: a sessão estava em fallback GPU.

Rodada adicional: MSE 720p → 1080p por 24 s passou com 709 frames decodificados,
zero descartados na última amostra, composição GPU ativa e seleções melhoradas
registradas nas gerações 1 e 2 (marcos 120/240 acumulados). Relatório
`validation/vsr-1791323389774520888/report.json`. Isso valida essa troca específica,
não toda a matriz MSE. Integração nativa modos 4/11 passou 2/2 após a correção
VA-API (`validation/av1-fix-native-tests.log`). A janela interativa foi reiniciada
com o mesmo perfil e restauração das abas para carregar a correção.

## Continuidade do VSR / piscada entre original e melhorado — 2026-10-06

O usuário identificou alternância visual, especialmente em 360p/480p. O antigo
agendamento marcava frames como vistos antes de conseguir submetê-los. Frames
recusados por dois pedidos ocupados/pool cheio não eram tentados novamente, e
conclusão/liberação de lease não retomavam a fila. Contar seleções aprimoradas
isoladas não comprovava continuidade.

Correção: só marcar seen depois do envio efetivo; visitar a fila já pertencente
a VideoRendererAlgorithm em ordem, sem criar outra fila de leases de decoder;
retomar a partir de enqueue, nova apresentação, conexão, conclusão/falha e
retorno do lease. Mantidos os três slots de saída, dois pedidos pendentes,
um worker CUDA, os níveis 4/11 e watchdog de 100 ms. Frames já apresentados
não são trocados retroativamente, nem substituídos por frames antigos.
O acesso à fila está em `chromium/patches/vsr-queue-lookahead.patch`, aplicado
por apply.py. O patch foi verificado contra o índice do Chromium pinado.

A revisão detectou riscos na troca de configuração. Cada ID agora conserva sua
geração de enqueue (mapa apenas de metadados, podado pela fila existente).
Configuração criptografada, inclusive a inicial, ativa um bypass conservador
até o fim da vida do renderer, mesmo se depois passar para clear: o observer
pode preceder o drain de frames antigos. Isso preserva o requisito sem DRM.

Métricas: janelas de 120 decisões únicas, reiniciadas a cada geração, contam
melhorados/originais/trocas. `--require-continuous-vsr` exige também o gate VSR,
ignora apenas a primeira janela de cada player/geração como aquecimento e
reprova qualquer original nas janelas seguintes. Não cobre a cauda incompleta
de até119 frames e não exige janela pós-aquecimento em toda geração.

Testes: BusyFrameCanBeRetriedAfterCapacityReturns falhou com o código anterior
(2 pedidos, esperado3), passou após a correção. Suíte Python 18/18; seleção
Chromium renderer/algorithm/metadados/IPC 95/95, incluindo configuração inicial
criptografada, encrypted→clear e geração conservada na fila.

360p local por20s passou o gate de continuidade, com janelas pós-aquecimento
120/120 em ambas as gerações. Relatório local:
`validation/vsr-1791324743673463378/report.json`.
YouTube real360p: quatro janelas pós-aquecimento120/120, zero originais/trocas
(`validation/youtube-360p-1791324833714312863/report.json`). YouTube480p na versão
revisada: cinco janelas pós-aquecimento120/120 (600 decisões), zero
originais/trocas (`validation/youtube-480p-1791325062886046757/report.json`).
Houve originais no aquecimento. Rodadas anteriores desta mesma investigação
registraram faltas na segunda janela, portanto não se afirma ausência absoluta
de falhas sob toda carga; os relatórios finais comprovam os intervalos medidos.

Reprodução do gate (com NVVFX_BROWSER_BUILD_ROOT e VFXSDK_ROOT configurados):
`python3 tools/chromium/playback-check.py --clip h264-360p-30.mp4 --seconds 20 --require-continuous-vsr`.
`generate-media.sh` gera também fixtures360p/480p, disponíveis no player local.

### Margem de saídas e regressão final1080p

A comparação posterior encontrou3 originais numa janela pós-loop1080p com
três saídas (`vsr-1791325143570717317`), enquanto720p passou
(`vsr-1791325120581734356`). O pool de ClientSharedImages de saída foi ampliado
para cinco. A admissão continua em dois pedidos e o pool privado GPU/worker
continua em três; mailbox de saída e índice privado são independentes. Custo
extra máximo de15,82MiB por sessão a1920x1080 RGBA/RGBX. Não se adicionou uma
fila de frames do decoder nem atraso ao relógio de apresentação.

O teste de pool agora preenche cinco saídas, bloqueia o sexto pedido até retorno
de lease e verifica o fence do consumidor no reuso; falhou RED com pool3 e passou
GREEN com pool5. Os95 testes Chromium passaram na configuração final.1080p por
24s, incluindo retorno ao início, passou continuidade nas janelas após cada
warmup (`validation/vsr-1791325257459845930/report.json`, duas janelas120/120
na segunda geração). Essa rodada suporta o ajuste de margem, sem provar que
qualquer carga futura ficará sem misses. O orçamento global256MiB da spec não
está explicitamente imposto e múltiplas sessões aumentam a memória; essa é uma
lacuna anterior ao aumento e não uma validação de orçamento global.

### Corrida entre inferência e publicação

Diagnóstico temporário por frame reproduziu os dois originais na segunda
janela em `validation/youtube-switch-1791325797277981470/report.json`: IDs338
e351, PTS7,007s e7,215541s, submetidos e sem conclusão aprimorada. O log
registrou `other_running` nas rejeições correspondentes. O callback do worker
apenas postava o resultado no owner, e `Running` persistia até `FinishOutput`;
o próximo Run podia ser rejeitado enquanto a inferência anterior já acabara.

Adicionado estado `Publishing`: `OnWorkerFinished` roda diretamente no worker,
encerra `Running` sob lock antes do próximo Run e então posta no owner.
A reserva continua ocupada; somente `CompleteSafely`, após publicação, permite
reuso. Quarentena desabilita a admissão imediatamente. Regressão cobre
A em publicação, B iniciando, C bloqueado e ausência de watchdog após término
físico. RED: método inexistente; GREEN: teste passou. CPU7/7 e Python18/18.

Ocorrência de build nesta investigação: uma invocação acidental do Ninja do
sistema1.13.2 encontrou o log anterior1.12.1 como antigo e reiniciou o histórico
incremental. Interrompido o rebuild amplo; não houve limpeza da árvore nem dos
artefatos existentes. As unidades modificadas e bibliotecas media/GPU foram
recompiladas/relinkadas usando os comandos exatos do grafo GN, com exit0
(`validation/continuity-publication-build.log`). O Rust core cujo rebuild havia
sido interrompido foi recompilado antes do link. O histórico incremental não
foi reconstruído artificialmente: próximo autoninja poderá revalidar/recompilar
muitas unidades. Usar consistentemente o Ninja do checkout via autoninja.

Validação final da corrida corrigida: YouTube real por45s, troca360p→480p,
`validation/youtube-switch-1791325882950346960/report.json`. Dimensões medidas
640x360 e854x480. Gate saúde/seleção/continuidade passou; processo GPU sem
quedas, composição e decode acelerados. Geração1: aquecimento114/120, duas
janelas seguintes120/120; geração2: aquecimento115/120, três seguintes120/120.
Total600 decisões pós-aquecimento, zero originais e zero alternâncias nessas
janelas. Não cobre intervalos parciais, scanout físico nem todas as cargas.
Suítes finais após a correção: CPU7/7, Python18/18, Chromium media95/95 e
integração nativa2/2. Diagnóstico temporário por-ID removido do código final.

Regressão1080p final por24s incluindo loop também passou
(`validation/vsr-1791325954409274505/report.json`): janelas completas após
aquecimento120/120 nas duas gerações. Chromium experimental reiniciado com
perfil preservado e `--restore-last-session`; browserPID1217012, GPU1217082,
crash-count0. Log atual `validation/interactive-vsr-current.log`; log anterior
arquivado em `validation/interactive-vsr-before-continuity-fix.log`.


## 2026-10-06 — VSR somente no player em tela cheia

Implementado gate por player no Blink/compositor e renderer, com época para
invalidar resultados antigos e bypass em janela. Callback assíncrono restaura
original ao sair pausado; o compositor só substitui o ID aprimorado esperado,
sem rejeitar restauração por reentrada posterior. Mantém modelo carregado.
F11 sozinho, modo teatro e PiP não ativam o efeito. Fullscreen de wrapper externo
com iframe cross-origin não foi validado. Patch: vsr-fullscreen-gate.patch;
novo video_renderer_sink.cc distribuído no overlay e aplicado somente com ele.

Verificações: media104/104, integração GPU nativa2/2, Python18/18; patch aplica
sobre índice HEAD limpo e reverse-check da árvore compilada passou.
YouTube: validation/fullscreen-gate-final-1-1791330498430226171/report.json.
Sem aprimoramento em janela inicial; fullscreen ativou inferência1080p; pausa,
saída e reprodução em janela mantiveram contador240 sem novos aprimoramentos;
GPU crash-count0. Houve3 frames descartados no total durante carregamento/entrada.
Teste local360p contínuo20s passou,480 seleções aprimoradas:
validation/vsr-1791330511701196073/report.json.

A demora fullscreen NÃO está resolvida. Último teste: entrada3.81s e saída1.11s
(entrada inclui mudança de qualidade); medições anteriores também lentas com VSR
DESLIGADO. Trace desligado mostra tarefas longas no RendererMain/JavaScript/layout
do YouTube. Flags reduced-motion não resolveram e não foram adotadas. Build é
is_debug=false, mas mantém DCHECK_ALWAYS_ON e expensive dchecks por configuração
padrão não oficial; desligá-los requer rebuild consistente, não troca parcial de
bibliotecas. Não prometer ganho de latência sem comparação medida.


## 2026-10-06 — nitidez após VSR/denoise

Pedido do usuário: melhorar detalhes/nitidez, inclusive vídeo1080p no monitor1080p.
Adicionado pós-filtro oficial `NvCVImage_Sharpen` na GPU após VSR/denoise, antes
da cópia final e do evento de conclusão. O SDK pinned suporta RGB8 interleaved;
as conversões RGBA→RGB→RGBA ficam na GPU e a saída permanece opaca. Buffers RGB
persistentes e scratch com borda de1pixel seguem o drain/quarentena existentes;
nenhum frame adicional, inferência extra ou transferência de pixels para CPU no
playback. Readback somente no teste GPU diagnóstico. Mantém fullscreen-only e
bypass protegido/HDR existentes. Dois buffers RGB1080p somam aproximadamente12MiB.

Launcher `tools/chromium/run.sh` habilita intensidade leve0.35 por padrão.
`NVVFX_VSR_SHARPNESS=0` restaura a imagem anterior mantendo VSR/denoise. Intervalo
aceito0..1; configuração ausente no core, inválida/não finita ou símbolos opcionais
ausentes deixa apenas VSR. Valor lido na criação do processor: reiniciar o navegador
para comparar. Não significa recuperar detalhes perdidos nem renderizar em4K.
Mais intensidade pode salientar halos/ruído. Aparência final depende do conteúdo
e precisa de avaliação visual do usuário; teste sintético não prova preferência.

Validação RED: novo teste falhou com "post-sharpen did not increase local contrast"
antes da implementação. GREEN: contraste local do padrão2.41275e6→2.46334e6,
alpha255, mudança de tons limitada e `nan` idêntico ao modo desligado. Também
verifica primeiro frame e padrões de cores alternados nos modos4/11 com filtro.
Core CTest12/12 após pré-alocação final, nativeGPU2/2, Python18/18.
Artefatos locais: validation/sharpen-final-core-tests.log,
validation/sharpen-final-native-tests.txt e validation/sharpen-1080-benchmark.txt.
Benchmark curto100frames1080p modo11: p50 9.89ms desligado /10.34ms nitidez0.35;
p95 10.81/10.83ms, sob carga variável do desktop. Não garante60FPS em toda carga.
Playback local1080p30 passou24s/600 seleções aprimoradas com continuidade:
validation/vsr-1791331038689474973/report.json (antes da otimização de scratch).
Backend atualizado/relinkado em libgpu_ipc_service.so E libcontent.so, além do
alvo gl_tests, porque o grafo contém cópias do core em ambas bibliotecas.

Fonte API: https://docs.nvidia.com/maxine/nvcvimage/latest/index.html e header
oficial do SDK instalado (`nvCVImage.h`, comentário de NvCVImage_Sharpen).

YouTube após scratch final: validation/fullscreen-gate-final-1-1791331092378385905/report.json,
GPU crash-count0, janela pós-aquecimento120/120 aprimorados, zero originais/trocas;
240 seleções totais, contador estável ao sair pausado e retomar em janela.
Adicionado log de inicialização `NVIDIA VSR post_sharpen=...` para registrar valor
real dentro do processo GPU (não confiar em /proc/environ, que Chromium altera).
Teste final720p registra `post_sharpen=0.35` em browser.log. Ajuste por ambiente
exige encerrar o processo do mesmo perfil; abrir outra janela não reconfigura.

Playback final720p30: validation/vsr-1791331173394784883/report.json,20s,480 seleções aprimoradas, gate de continuidade passou e logGPU post_sharpen=0.35. Chromium interativo reiniciado com --restore-last-session e novo padrão0.35; log validation/interactive-vsr-current.log, anterior arquivado em interactive-vsr-before-sharpen.log.

## 2026-10-06 — supersampling 1440p

Launcher agora usa NVVFX_VSR_TARGET_HEIGHT=1440 por padrão. Política compartilhada
renderer/GPU seleciona VSR_ULTRA modo4 strength1.0 para fonte até1080p, saída
2560×1440 mantendo proporção; natural_size permanece original e compositor reduz
para o monitor1080p. NVVFX_VSR_TARGET_HEIGHT=1080 restaura modo11 para fonte nativa.
Post-sharpen permanece0.35. Fontes acima1080p e protegidas continuam bypass.

Verificação:12/12 CTest,105/105 media,2/2 GPU nativo,18/18 Python. Teste GPU
100 frames1080→1440 com sharpen: p50~7.44ms,p95~7.82ms (isolado, não garantia60FPS).
Playback local1080p30 por20s: validation/vsr-1791332645045696252/report.json,
480 seleções aprimoradas; fullscreen40/40 samples; janela pós-aquecimento120/120
aprimorados sem original/troca. Loop do clip reinicia geração e requer aquecimento.
Log GPU confirmou mode4 input1920x1080 output2560x1440 post_sharpen0.35.
Comparativo1080: validation/vsr-1791332674268911206/report.json, também passou.

GPU1440 (nvidia-smi, amostras6–20 em validation/1440-local-gpu.csv): média34.07%,
pico40%; VRAM média4001MiB/pico4051MiB; potência média59.12W. São totais da placa,
incluindo desktop, em fonte sintética1080p30; não extrapolar para YouTube60FPS.
A/B1080 coletado, mas relógios/carga não controlados; não inferir ganho de consumo.
Probes longos YouTube saíram de fullscreen; medições descartadas e causa ainda
não determinada. Trace confirmou saída DOM real, corrigindo interpretação inicial
de falha heurística. Usar gate DOM; remover traces temporários antes de relink final.


## Public preview gate — 2026-10-06

The user approved local 4K supersampling. Public distribution adds a stricter
release gate: real fullscreen inference with all GPU threads sandboxed, using
an external SDK. This gate has NOT passed; no binary release is authorized by
these results alone.

- Runtime resolver, worker and broker now agree on absolute `VFXSDK_ROOT`.
- CTest 12/12, Python 27/27 (installer 9/9), actual GPU seccomp scheduler filter
  10 cases passed. The scheduler additions permit only read-only min/max queries
  for SCHED_OTHER; other policies return EINVAL. No scheduling changes allowed.
- TSYNC alone exposed the existing broker single-thread DCHECK. Broker fork now
  precedes VAAPI initialization when early sandbox/native NVIDIA EGL are selected;
  Perfetto stops once and restarts after seccomp. Existing DCHECKs retained.
- `vsr-1791335626500392868`: SystemInfo sandboxed=true, zero GPU crashes,
  every observed GPU thread (including CUDA) Seccomp=2, but zero enhanced frames.
  Detailed thread evidence: public-preview-priority-query-threads.json.
- A driver/SDK symbol collision caused Shutdown1 crashes after a tentative
  RTLD_GLOBAL driver preload. Driver NGX is now RTLD_LOCAL; final reproduction
  `vsr-1791336130055926841` exited normally with effect load -14 and no enhancement.
- Temporary broker diagnostics showed the SDK probing
  `$VFXSDK_ROOT/lib/../features/nvvfxvideosuperres`; Chromium intentionally rejects
  parent/self references. Do not relax that validation or grant broad filesystem
  access to hide the problem. SDK/NGX discovery must be initialized through a
  supported path, or redesigned, before distributing a browser binary.
- Diagnostic broker logging was restored to upstream and recompiled; it is not
  included in the patches. Early sandbox is now explicitly diagnostic opt-in via
  NVVFX_VSR_SANDBOX_EXPERIMENT=1. Packaged launcher requires early sandbox and
  fatal sandbox initialization failures. Source/tooling publication is separate
  from binary release acceptance.

Restored development path verification: `vsr-1791336265870619909`, 20 seconds
of fullscreen 1080p30 →3840×2160, 480 enhanced selections, continuous post-warmup
windows, clean exit. This does not satisfy the public sandbox gate.

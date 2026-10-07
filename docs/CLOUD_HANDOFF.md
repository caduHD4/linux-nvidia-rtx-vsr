> Estado da distribuição pública — 2026-10-06: o playback local fullscreen-only
> com alvo 4K foi aprovado visualmente pelo usuário, mas a sessão funcional
> anterior reportava GPU `sandboxed=false`. Na investigação atual, SystemInfo
> reporta sandbox ativo e todos os threads GPU observados têm `Seccomp: 2`;
> porém a descoberta NGX falha com `-14`: o SDK consulta `lib/../features`,
> rejeitado pelo broker. Isso **não** valida inferência sob sandbox.
> Não publicar binário até playback aprimorado real passar com sandbox ativo.
> Verificado nesta rodada: CTest 12/12, Python 27/27 (instalador 9/9) e 10 casos
> de restrição do escalonador no filtro seccomp real. Consulte também
> [o plano de distribuição](superpowers/plans/2026-10-06-public-preview.md).
>
> As medições e notas abaixo preservam o histórico local. Afirmações antigas de
> “sandbox ativo” baseadas apenas na ausência de flags que o desativam estão
> corrigidas pelo estado acima; não constituem evidência de sandbox GPU.

> Atualização de continuidade em06/10: corrigidas a corrida admissão/publicação
> (`Publishing`) e a restauração do contexto em `FinishSource`. YouTube360→480p
> passou600 decisões completas pós-aquecimento sem originais/trocas, sem queda
> GPU. Ver evidências e ocorrência de perda do histórico incremental do Ninja
> no final de `docs/chromium-validation.md`; próximo build amplo pode refazer
> muitas unidades. Artefatos locais atualizados com compilação/link das unidades
> modificadas; CPU7/7, Python18/18, media95/95, native2/2.

# Continuidade para GPT Work / Codex na nuvem

## Leia primeiro

O objetivo é integrar NVIDIA RTX Video Super Resolution no pipeline de vídeo do
Chromium para Linux e depois portar para Brave. O usuário aprovou continuar
até um navegador utilizável, sem novas perguntas de rotina. O desenvolvimento
local foi retomado em 2026-10-06 após o ambiente da nuvem ficar sem memória.
**Chromium com VSR já foi validado em playback HTML5 real no host original.**
O executável e o SDK ficam fora deste repositório; consulte as instruções de
uso local abaixo. Brave ainda não foi portado.

### Atualização histórica: continuidade visual

A correção da piscada está descrita na seção “Continuidade do VSR” ao final:
agendamento da fila existente, metadados de geração, bypass conservador de
criptografia e novo gate `--require-continuous-vsr`. Na rodada final YouTube
360p/480p teve janelas completas sem originais após aquecimento. Leia as
limitações dessa seção; os snapshots históricos abaixo não comprovam continuidade.

### Registro histórico de 2026-10-06 (17:17 BRT) — correções ao final deste documento

- Playback real com VA-API NV12, EGL nativo no Wayland, sem flags para desativar sandbox,
  RTX 4070 SUPER e SDK VFX 1.3.0.0 passou `--require-vsr`: 720p/30 apresentou
  120 frames aprimorados em 12 s (modo VSR_ULTRA 4, ~18,5 ms GPU/frame); 1080p/30
  apresentou frames processados sem redimensionamento (DENOISE_ULTRA modo 11,
  ~12,8 ms GPU/frame). Os vídeos avançaram com 5 e 4 frames descartados,
  respectivamente, incluindo a inicialização do SDK.
- A reprodução usa `/home/caduhd4/Builds/brave/src/out/Vsr/chrome` e o launcher
  `tools/chromium/run.sh`. Para o frame real NV12 foi necessário habilitar EGL
  nativo no Ozone Wayland quando VSR está ligado. ANGLE não expõe este backing
  VA-API como representação GL utilizável neste driver.
- Foram removidas a trava global de uma sessão (que rejeitava o novo player
  enquanto o anterior encerrava) e as mensagens de log temporárias. A política
  ainda elimina frames cuja fila expirou durante o carregamento inicial do SDK.
- As evidências reproduzíveis e diretório dos relatórios estão em
  [chromium-validation.md](chromium-validation.md). A validação cobre playback
  Chromium e processamento/seleção para apresentação (GPU sandbox não validado); scanout físico
  e porte Brave ainda não foram medidos/concluídos.

- Cliente IPC/pool, serviço GPU, worker CUDA e hook pré-sandbox estão
  implementados no overlay completo. Após as correções de lifetime, o alvo
  `media_unittests` compilou e os 59 testes selecionados de IPC/cache/
  VideoRendererImpl e metadados passaram na rodada final.
- Revisões independentes identificaram retenção desnecessária de frames do
  decoder, possível liberação de texturas após falha de teardown e perda do
  fence do consumidor quando a sequência media encerra. As correções estão
  implementadas; os testes Chromium e o playback real passaram.
- Cache libera saídas concluídas de frames pulados, preservando a escolha do
  frame atual e resultados futuros. Os 12 testes standalone CPU/GPU passaram
  após as mudanças de cache e encerramento explícito da ponte GL/CUDA.
- Benchmark GPU standalone com 10.000 frames por modo: modo 4 p50/p95/p99 de
  2,76070/3,02285/3,12218 ms; modo 11 de 9,62662/10,1137/10,3188 ms. A nova
  rodada dos testes, incluindo primeiro frame e padrões alternados, passou
  12/12.
- Testes Chromium devem incluir `NvidiaVsr*:VsrClientIpcTest.*` e os testes
  existentes do `VideoRendererImpl`. Compile alvos específicos passando-os
  a `build.sh`; sem argumentos o alvo padrão continua sendo `chrome`.
- Regressão GPU passou 25/25 testes de `GpuChannel`/`GpuChannelManager` e
  compound backing. A factory SharedImage teve 5 aprovados e 1 skip por
  `WEBGPU_SHARED_BUFFER` não suportado. Os dois casos nativos
  passaram: modos 4/11 com três padrões por modo, recorte/orientação/clipping
  e bypass protected/encrypted antes de import. Usam fonte RGBA sintética e saída RGBX opaca, sem prova de NV12 do decoder VAAPI.
  É um teste sem sandbox Chromium, não playback. `NvidiaVsrNativeEgl` permite
  testar EGL nativo sem inferência VSR.
- `CU_STREAM_NON_BLOCKING` causava saída preta/atraso de um frame com a
  ordenação interna do NGX; o stream VFX agora usa `CU_STREAM_DEFAULT`. No teardown,
  o owner mantém as referências GL e recursos do worker com conclusão incerta
  ficam em quarentena. A mudança compilou e a fixture nativa não gerou DCHECK.
- O renderer registra seleções aprimoradas para apresentação, deduplicadas
  por geração/ID entre refreshes. O checker `--require-vsr` exige essa evidência;
  completions do SDK sozinhos não bastam. As regressões de mídia passaram
  56/56 e os testes Python 10/10. Playback real está validado; scanout físico
  continua sem medição.
- Brave e testes estendidos de troca de fonte, MSE, dois vídeos, pause/seek e
  scanout físico continuam pendentes. A tabela abaixo registra o estado atual.


Comece pelo [design aprovado](specs/2026-10-05-chromium-nvidia-vsr-design.md),
[pelo plano](superpowers/plans/2026-10-05-chromium-nvidia-vsr.md) e pelo
[registro de decisões/progresso](development-progress.md). Este documento
prevalece sobre frases antigas de status que dizem que o core ainda não existe.

## Estado atual

| Componente | Estado em 2026-10-06 |
|---|---|
| M0/M1 e core persistente | Implementados; suíte standalone passou 12/12, incluindo primeiro frame e padrões alternados. Modos 4 e 11 exercitados na RTX 4070 SUPER. |
| `GlVsrBridge`, elegibilidade, política, admissão e cache | Implementados e cobertos por testes standalone; a ponte EGL/CUDA foi exercitada separadamente. |
| Chromium fixado e ferramentas de build | Checkout no host original; revisão `b510e9d7cd3a2fbd78d0ddc42234103206c5f78d`. |
| Probe, cache de apresentação e cliente renderer IPC/pool | Implementados no patch/overlay. `media_unittests` compilou e 59 testes selecionados de IPC/cache/VideoRendererImpl/metadados passaram. |
| Serviço GPU/Mojo e worker CUDA | Implementados e compilados; fixture nativa pelo caminho Mojo/Skia→GL privado→CUDA/VFX→SharedImage passou em modos 4 e 11 com quadrantes alternados, recorte/orientação/clipping e bypass protected/encrypted. Fonte RGBA sintética e saída RGBX opaca; não valida NV12/VAAPI, sandbox ou playback. |
| Hook pré-sandbox | Implementado e compilado. Seccomp ativo observado em todos os threads GPU na tentativa atual; inferência bloqueada por falha NGX/effect load -12. |
| Chrome e playback HTML5 | Playback local VA-API NV12/VFX validado anteriormente, com GPU sandboxed=false. Fullscreen-only e alvo 4K testados pelo usuário. Release binário aguarda inferência funcional com sandbox ativo. |
| Brave | Não iniciado; depende da validação completa do Chromium. |

As mudanças recentes incluem `f2f437a` (cliente renderer e serviço GPU) e
`b7bbb62` (retenção segura do contexto VFX após teardown inseguro). O build do
navegador, os arquivos gerados, o SDK, caches e mídia ficam fora do repositório.
O estado histórico da transferência inicial para a nuvem está em
[local-history.md](local-history.md); seus números e pendências não descrevem o
estado atual deste worktree.

## Ambiente e requisitos

Use Linux x86_64. O host original tem Ryzen 5 5600X, 31 GiB RAM, RTX 4070 SUPER
12 GiB (`sm_89`), driver NVIDIA 615.71.09. Monitor alvo 1920x1080, 180 Hz.
Para um build Chromium completo, tenha RAM/disco suficientes: o volume local
foi de 147 GiB, com reserva de 20 GiB, mas isso **não prova** que Chromium e
Brave completos cabem juntos. Verifique espaço continuamente. Use um único
checkout quando possível. O paralelismo padrão é 8, ajustável por
`NVVFX_BUILD_JOBS`.

Na nuvem sem GPU ou sem SDK, execute os testes CPU e trabalhe no código/patch.
A compilação com backend precisa dos headers oficiais, mas não precisa executar
CUDA. Playback e SDK reais precisam de NVIDIA, driver compatível, runtime,
contexto EGL NVIDIA e acesso ao display. Não trate um build sem GPU como prova
de VSR, nem invente headers/ABI NVIDIA para contornar a ausência do SDK.

Instale ferramentas básicas: Git, Python 3, CMake >=3.25, compilador C++20,
Ninja e dependências de build do Chromium. O checkout baixa as ferramentas
Chromium oficiais. Para Ubuntu, consulte o script
`src/build/install-build-deps.sh` antes dos hooks; se faltarem pacotes, execute
esse script no ambiente de build e repita o setup. Referência oficial:
[Chromium Linux build instructions](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md).
FFmpeg só é necessário para gerar os vídeos sintéticos de teste.

## SDK NVIDIA externo

Core e feature testados: **1.3.0.0**. Obtenha o Core Linux pelo NGC, conforme
[a instalação oficial NVIDIA](https://docs.nvidia.com/maxine/vfx/latest/LinuxVFXSDK/InstalltheVFXSDK.html).
Extraia o pacote em um diretório externo/ignorado e use `VFXSDK_ROOT` absoluto.
O instalador de features fornecido pelo SDK usa `NGC_CLI_API_KEY`; configure
essa credencial como segredo do ambiente, nunca no Git. No diretório
`VideoFX/features`, instale `nvvfxvideosuperres` versão 1.3.0.0 com o script
`install_feature.sh` oficial (flags `-f nvvfxvideosuperres -v 1.3.0.0`).
Consulte `--help` para seleção de GPU/arquitetura. O diagnóstico local usou
artefatos para compute capability 8.9; não assuma compatibilidade com qualquer
GPU. Downloads podem depender das permissões da conta NGC.

Arquivos esperados incluem:

```text
VideoFX/include/{nvVideoEffects.h,nvCVImage.h}
VideoFX/external/cuda/include/{cuda.h,cudaGL.h}
VideoFX/lib/{libVideoFX.so,libNVCVImage.so,...dependências oficiais...}
VideoFX/features/nvvfxvideosuperres/include/nvVFXVideoSuperRes.h
VideoFX/features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so
```

Não publicar bibliotecas, modelos, pacote do SDK, headers proprietários ou
credenciais. Os três headers vazios em `tests/cmake/fake-sdk` são apenas
fixtures de descoberta CMake e não substituem os headers reais.

## Reproduzir o estado Chromium

Base exata: tag **154.0.8037.97**, commit
`b510e9d7cd3a2fbd78d0ddc42234103206c5f78d`.

```bash
# Execute a partir da raiz deste repositório.
export NVVFX_BROWSER_BUILD_ROOT="$PWD/build/browser"
export VFXSDK_ROOT="/caminho/absoluto/VideoFX"
export NVVFX_BUILD_JOBS=8
bash tools/chromium/checkout.sh
python3 tools/chromium/apply.py
bash tools/chromium/build.sh media_unittests third_party/nvidia_vsr:backend
```

`checkout.sh` baixa depot_tools, configura `.gclient` com URL fixada no SHA,
faz fetch raso, sync sem histórico e hooks. Não remova o pin: um URL não fixado
levou a consultas remotas de HEAD muito demoradas neste host. Dependências,
CIPD e vpython ficam no mesmo build root. O build root padrão é
`build/browser` dentro do projeto (ignorado).

`apply.py` aplica `chromium/patches/chromium-current-wip.patch` e chama
`stage-core.sh`. O modo padrão restaura o core e o probe. Com `--overlay`, copia
a implementação Chromium completa de cliente renderer, cache, serviço GPU,
worker e hook pré-sandbox. O overlay compilou junto com `chrome` e passou playback local no host NVIDIA;
isso não comprovou sandbox GPU ativo. O alvo `:backend` compila o core. O script de build habilita
`enable_nvidia_vsr=true` e usa os headers externos; o SDK é carregado
dinamicamente pelo core.

Para restaurar a implementação Chromium completa em um checkout recém-preparado:

```bash
python3 tools/chromium/apply.py --overlay
```

A ordem é patch primeiro, overlay depois. O overlay contém cópias modificadas
de arquivos Chromium e arquivos novos; ele sobrepõe alguns arquivos do patch.
Use um checkout limpo na revisão fixada ao reaplicar. O overlay, o executável
`chrome` e playback local foram validados; inferência com sandbox GPU ativo
continua bloqueada. Brave continua
pendente.

Sem SDK, para compilar o probe/core policy sem o backend NVIDIA:

```bash
NVVFX_ENABLE_NVIDIA_VSR=0 bash tools/chromium/build.sh media_unittests
```

Configuração GN de desenvolvimento: release component build, símbolos zero,
`use_remoteexec=false`, `use_siso=false`, `proprietary_codecs=true`,
`ffmpeg_branding="Chrome"`, `enable_validating_command_decoder=true`.
`build.sh` grava `out/Vsr/args.gn`; alterações manuais desse arquivo serão
substituídas. O backend habilitado requer `nvidia_vsr_sdk_root` absoluto.

## Testes já existentes

```bash
cmake -S . -B build/cpu -DNVVFX_VSR_BUILD_SMOKE=OFF
cmake --build build/cpu --parallel 2
ctest --test-dir build/cpu --output-on-failure
python3 -m unittest discover -s tests/python -v

cmake -S . -B build/gpu -DCMAKE_BUILD_TYPE=Release -DVFXSDK_ROOT="$VFXSDK_ROOT"
cmake --build build/gpu --parallel 2
ctest --test-dir build/gpu --output-on-failure
```

Baseline da rodada de distribuição em 2026-10-06: CTest standalone 12/12 e
Python 22/22. As contagens Chromium abaixo são registros de rodadas anteriores,
não uma nova execução completa do navegador nesta rodada. A nova
execução CTest com padrões alternados também passou 12/12. Os testes GPU incluem
readback/upload **somente nas fixtures de diagnóstico**, nunca na reprodução.
O teste EGL usa `/usr/share/glvnd/egl_vendor.d/10_nvidia.json` e `EGL_PLATFORM=x11`;
adapte o caminho ao host. O dispatcher padrão selecionou llvmpipe e falhou
corretamente na validação CUDA GL. O teste foi comprovado com NVIDIA EGL
nativo, não com IDs de textura ANGLE.

Para repetir a suíte Chromium validada:

```bash
"$NVVFX_BROWSER_BUILD_ROOT/src/out/Vsr/media_unittests" \
  --gtest_filter='NvidiaVsr*:VsrClientIpcTest.*:VideoRendererImplTest.*' \
  --test-launcher-jobs=4
```

O filtro inclui explicitamente os casos IPC, cache e regressões existentes do
renderer. Os 56 casos passaram. Isso valida testes unitários e mocks; não
comprova processamento de vídeo no navegador.

O fixture nativo do serviço GPU e os resultados de latência standalone, assim
como a invocação do caso `DISABLED` e os limites dessa evidência, estão em
[chromium-validation.md](chromium-validation.md). O fixture provou
os modos 4/11 e saída de cores alternadas pelo caminho do serviço; ainda não
provou sandbox, playback, decoder real ou integração de navegador.

Mídia sintética, quando FFmpeg com libx264/libvpx-vp9/libsvtav1 estiver disponível:

```bash
bash tools/chromium/generate-media.sh
python3 -m http.server 8000 --directory "$NVVFX_BROWSER_BUILD_ROOT/media"
```

Para playback, use os launchers e o checker descritos em
[chromium-validation.md](chromium-validation.md). Os clipes incluem H.264
720p30/60 e 1080p30, VP9/AV1 720p30 e transição MSE 720p→1080p. O launcher
configura EGL, decoder validating e a feature, mantendo o sandbox habilitado.
Playback local foi executado; inferência com sandbox GPU ativo ainda não passou.
Nunca adicionar `--no-sandbox`.

## Decisões obrigatórias para a implementação restante

- Primeiro Chromium; Brave só após validação real Chromium.
- Upscaling para o alvo 1080p: VSR_ULTRA modo 4, strength 1.0. Native 1080p:
  DENOISE_ULTRA modo 11, mesma geometria. Acima do alvo: bypass.
- Sem DRM: track criptografada, protected/hw_protected e usage de recurso
  protegido fazem bypass **antes de import/BeginAccess/SDK**. HDR/10-bit/P010,
  alpha, formato desconhecido, CPU, rotação e pixels não quadrados também.
- Um processamento por frame decodificado; nunca por refresh 180 Hz.
- O `VideoRendererAlgorithm` recebe originais. Seleção de saída só se pronta
  no corte de apresentação, decisão congelada por generation/frame ID;
  seek/flush/config change invalidam generation. Original como fallback.
- Nenhum pixel de playback passa pela CPU. Copiar/converter GPU é permitido.
- Um serviço GPU ativo; uma inferência ativa + uma aguardando, 3 slots,
  limite de 256 MiB, deadline de fila 50 ms, watchdog >100 ms desliga admissão
  sem liberar recursos cuja conclusão física não foi comprovada.
- Owner GPU faz Skia/Ganesh crop/NV12 -> RGBA em input privado. CUDA worker
  usa contexto nativo compartilhado no mesmo dispositivo físico. Só texturas
  privadas de input/output ficam registradas CUDA; após unmap/drain, owner
  copia output privado -> SharedImage de saída. SharedImage entregue ao
  renderer **nunca** deve permanecer mapeado/registrado no CUDA.
- Fences de source copy e de output final são independentes. Owner poll GL
  fence sem bloquear; worker tem outro GL fence no mesmo ponto. Não fazer
  a liberação da superfície original esperar o modelo/VFX.
- `VideoFrame::UpdateReleaseSyncToken` precisa ordenar release anterior:
  SyncTokenClient coleta token anterior nas dependências do source task e
  publica token conhecido de source copy. Nunca colocar espera por output
  VFX na sequência SharedImageInterface. Ack de criação do serviço deve
  estabelecer ambos sync-point client states antes de usar esses tokens.
- Esperas CUDA/interoperabilidade só no worker; não bloquear compositor nem
  segurar locks do backing. Falha/timeout: safedrain ou quarentena de todos
  os recursos privados/contexto até saída do processo GPU.
- Native EGL precisa do decoder validating existente. O decoder passthrough
  exige ANGLE por segurança; não enfraquecer seu CHECK.

## Próximos passos concretos

1. Conferir baseline patch e objetos/testes em checkout limpo. O patch preserva
   a alteração feita no Chromium local; `third_party/nvidia_vsr` é gerado a
   partir dos fontes do projeto por `stage-core.sh`, não está dentro do patch.
2. Fazer testes estendidos de troca de fonte, MSE, dois vídeos, pause/seek e
   medir scanout físico no desktop.
3. Revisar e expandir os testes do serviço/worker sob sandbox de navegador:
   preflight output
   representation/target antes de SDK; erros de alocação/GL fences nulos;
   término de semáforos/access no caminho de falha Skia; loss de contexto;
   lifecycle de sequências/receiver; desconexão do canal durante copy/inference;
   retenção segura/quarentena. `PollSourceFence` nunca libera original quando
   conclusão física é incerta.
4. Resolver a falha NGX/effect load -12 da tentativa com sandbox GPU ativo.
   O runtime funcionou anteriormente com GPU sandboxed=false; isso não libera
   distribuição binária. Em outro host, validar carregamento/permissões mínimas
   para SDK externo e dispositivos
   NVIDIA/CUDA. Falta de SDK deve virar
   bypass, não falha de startup nem flag sem sandbox.
5. Executar testes de playback/interoperabilidade e playback com sandbox
   habilitado. A/B, H.264/VP9/AV1, 720p30/60, nativo1080p, fullscreen/resize,
   pause/seek, troca de resolução, segundo player, SDK ausente, protected/HDR
   bypass, frames atrasados/pool cheio, memory plateau e inferências por frame
   único. Obter evidência de uso real do SDK/modo na reprodução e nenhuma cópia
   CPU.
6. Fazer revisão do conjunto, corrigir problemas e fechar a documentação de
   lançamento. Só então portar para Brave; mapear revisão Chromium compatível
   do Brave e orçamento de disco, sem alegar que um overlay do Chromium
   automaticamente funciona no Brave.

Não reiniciar M0/M1, não baixar um segundo checkout sem necessidade, não mudar
partições do host original e não publicar um navegador “pronto” baseado apenas
em testes standalone ou unitários.

## Histórico da publicação inicial na nuvem (2026-10-05)

Na publicação inicial, foram verificados CTest CPU/discovery 7/7, Python 6/6,
syntax dos scripts shell/Python, aplicação do patch em arquivos originais da
revisão fixada, staging do core e configuração do build com GN/Ninja mockados.
Naquela data, GPU e build Chromium não tinham sido retomados. Esses resultados
são apenas o registro histórico daquela publicação; veja o estado atual e
`chromium-validation.md` para as evidências atuais.

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

Supersampling1440 padrão: run.sh exporta NVVFX_VSR_TARGET_HEIGHT=1440; valor1080
restaura política anterior. Copiar overlay/core e aplicar vsr-fullscreen-gate.patch
(inclui HTMLVideoElement DOM gate). Ver resultados e limites de GPU1440 em
docs/chromium-validation.md. Build local validado por links GN selecionados devido
a incompatibilidade do cache Ninja; não assumir build completo incremental limpo.

Atualização posterior a pedido do usuário: padrão do launcher agora2160 (4K3840×2160);1080 e1440 são selecionáveis pelo mesmo NVVFX_VSR_TARGET_HEIGHT. Build atualizado; testes automatizados 4K não executados naquela rodada por solicitação explícita do usuário. Posteriormente o usuário aprovou visualmente o resultado 4K; resultados 1440 acima não validam desempenho ou sandbox em 4K.

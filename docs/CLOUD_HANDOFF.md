# Continuidade para GPT Work / Codex na nuvem

## Leia primeiro

O objetivo é integrar NVIDIA RTX Video Super Resolution no pipeline de vídeo do
Chromium para Linux e depois portar para Brave. O usuário aprovou continuar
até um navegador utilizável, sem novas perguntas de rotina. O desenvolvimento
local foi cancelado para transferir o trabalho à nuvem: **não há navegador
com VSR funcionando ainda**. A tarefa atual de publicação preserva o estado,
não conclui nem retoma a compilação interrompida.

Comece pelo [design aprovado](specs/2026-10-05-chromium-nvidia-vsr-design.md),
[pelo plano](superpowers/plans/2026-10-05-chromium-nvidia-vsr.md) e pelo
[registro de decisões/progresso](development-progress.md). Este documento
prevalece sobre frases antigas de status que dizem que o core ainda não existe.

## O que existe

| Componente | Estado no momento da transferência |
|---|---|
| M0/M1 SDK discovery e smoke | Implementados, testes Python/CPU/GPU locais passaram |
| Core persistente `VsrProcessor` | Implementado; modos 4 e 11 testados na RTX 4070 SUPER |
| `GlVsrBridge` | Interop CUDA com texturas privadas NVIDIA EGL desktop GL/GLES3 testado, inclusive contexto compartilhado em worker separado |
| Eligibility, target policy, cache e admission controller | Implementados e testados isoladamente |
| Chromium pin + toolchain/deps/hooks | Checkout local concluído; scripts publicados para reproduzir |
| Probe/classificação de VideoFrame + cache renderer | Patch preservado; objetos de produção/testes compilaram, testes Chromium ainda não executados |
| Serviço GPU / Mojo / metadata ownership query | Rascunho em `chromium/overlay/`, não compilado nem validado como conjunto |
| Cliente renderer com processamento IPC/pool de saídas | Ainda falta implementar; `Observe` no patch só registra metadados |
| SDK no sandbox / reprodução HTML5 real | Não implementado/validado |
| Brave | Não iniciado |

A compilação local foi interrompida pelo usuário em aproximadamente
13.270/53.453 etapas. Nenhum executável final `chrome` foi entregue. Os
arquivos compilados, SDK, caches, mídia e checkout Chromium não estão no repo.
O último commit do core antes da preparação deste pacote é `56af5ed`.
O histórico local de commits está em [local-history.md](local-history.md).

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
`stage-core.sh`. **Esse comando restaura o core e o probe, não o processamento
de pixels no navegador.** O alvo `:backend` compila o core, mas ainda não o
conecta à reprodução. O script de build habilita `enable_nvidia_vsr=true` e
usa os headers externos. GPU SDK é carregado dinamicamente pelo core.

Para inspecionar/continuar o serviço inacabado, em um checkout recém-preparado:

```bash
python3 tools/chromium/apply.py --overlay
```

A ordem é patch primeiro, overlay depois. O overlay contém cópias completas
modificadas de arquivos Chromium existentes e arquivos novos. Ele sobrepõe
alguns arquivos do patch; não aplique ambos em ordem inversa. **Há erros de
compilação conhecidos e falta conectar o cliente/sandbox.** Não apresente
`--overlay` como um build pronto. Idempotência do probe é reconhecida pela
aplicação reversa; depois de editar/aplicar overlay, use um checkout limpo ou
resolva as diferenças manualmente, sem descartar trabalho.

Sem SDK, para compilar somente o probe/core policy:

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

Último resultado local antes da transferência: CTest completo 11/11
(7 CPU/discovery + 4 GPU), Python 6/6 anteriormente. Os testes GPU incluem
readback/upload **somente nas fixtures de diagnóstico**, nunca na reprodução.
O teste EGL usa `/usr/share/glvnd/egl_vendor.d/10_nvidia.json` e `EGL_PLATFORM=x11`;
adapte o caminho ao host. O dispatcher padrão selecionou llvmpipe e falhou
corretamente na validação CUDA GL. O teste foi comprovado com NVIDIA EGL
nativo, não com IDs de textura ANGLE.

Para Chromium, depois de compilar/linkar:

```bash
"$NVVFX_BROWSER_BUILD_ROOT/src/out/Vsr/media_unittests" \
  --gtest_filter='NvidiaVsr*'
```

Esses testes não foram executados localmente: apenas os objetos foram
compilados. A variante de teste no overlay inclui um novo teste de release
SyncToken cujo helper ainda tem erro de estilo Chromium (veja abaixo).

Mídia sintética, quando FFmpeg com libx264/libvpx-vp9/libsvtav1 estiver disponível:

```bash
bash tools/chromium/generate-media.sh
python3 -m http.server 8000 --directory "$NVVFX_BROWSER_BUILD_ROOT/media"
```

Abra `http://127.0.0.1:8000/` no navegador experimental quando estiver pronto.
São clipes de 12 segundos H.264 720p30/60 e 1080p30, VP9/AV1 720p30 com BT.709
explícito, seleção/seek/fullscreen/segundo player. **Não existe launcher final
verificado.** Flags experimentais necessárias até aqui: `--use-gl=egl`,
`--use-cmd-decoder=validating`, `--enable-features=NvidiaVideoSuperResolution`
e, para o probe separado, `NvidiaVsrProbe`. Nunca adicionar `--no-sandbox`.

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

## Próximos passos concretos / erros conhecidos

1. Conferir baseline patch e objetos/testes em checkout limpo. O patch preserva
   a alteração feita no Chromium local; `third_party/nvidia_vsr` é gerado a
   partir dos fontes do projeto por `stage-core.sh`, não está dentro do patch.
2. Compilar o overlay incrementalmente. O novo helper
   `media/renderers/nvidia_vsr_source_release.h` foi testado em RED (header
   ausente) e depois **falhou** no plugin `chromium-style`: métodos virtuais
   com corpo não podem estar inline. Mover implementações para `.cc` e incluir
   no BUILD.gn; executar teste, não apenas compilar objeto.
3. Conferir APIs da revisão e dependências GN/Mojo/export symbols. O conjunto
   do serviço/worker/metadata query ainda não passou pelo compilador. Stub
   de build feature disabled e objetos de component build precisam funcionar.
4. Implementar FrameClient IPC real com GPUFactories/SII/GpuChannelHost,
   ack de criação, source acquire/previous-release dependencies, import de
   ownership, pool de 3 outputs/leases/release tokens, IDs deduplicados limitados,
   callback de conclusão e Renderer completion cache sob lock/generation.
   Revisar reentrância de `OnConfigChange`/locks e lifecycle/disconnect.
5. Revisar serviço antes de executar: preflight output representation/target
   antes de SDK; erros de alocação/GL fences nulos; término de semáforos/access
   no caminho de falha Skia; loss de contexto; lifecycle de sequências/receiver;
   desconexão do canal durante copy/inference; retenção segura/quarentena.
   `PollSourceFence` nunca libera original quando conclusão física é incerta.
6. Integrar `VsrProcessor::PreloadRuntime` antes do sandbox somente sob feature,
   abrir bibliotecas oficiais antecipadamente e adicionar permissões mínimas
   para SDK externo e dispositivos NVIDIA/CUDA necessárias. O helper apenas
   pre-carrega/version-check, sem criar efeito/CUDA stream/contexto. Ainda
   não há código de sandbox/pré-sandbox nesta integração. Falta de SDK deve
   virar bypass, não falha de startup nem flag sem sandbox.
7. Executar testes unitários/interop/falhas + playback com sandbox habilitado.
   A/B, H.264/VP9/AV1, 720p30/60, nativo1080p, fullscreen/resize, pause/seek,
   troca de resolução, segundo player, SDK ausente, protected/HDR bypass,
   frames atrasados/pool cheio, memory plateau e inferências por frame único.
   Obter evidência de uso real SDK/modo na reprodução e nenhuma cópia CPU.
8. Fazer revisão do conjunto, corrigir problemas, criar launcher/documentação
   que o usuário realmente consiga testar. Só então portar para Brave; mapear
   revisão Chromium compatível do Brave e orçamento de disco, sem alegar que
   um overlay do Chromium automaticamente funciona no Brave.

Não reiniciar M0/M1, não baixar um segundo checkout sem necessidade, não mudar
partições do host original e não publicar um navegador “pronto” baseado apenas
nos 11 testes do core.

## Verificações desta publicação

O pacote foi verificado novamente: CTest CPU/discovery 7/7, Python 6/6,
syntax dos scripts shell/Python, aplicação do patch em arquivos originais
da revisão fixada, segunda aplicação reconhecida, staging do core e restauração
do overlay. O script de build foi exercitado com GN/Ninja mockados em modos
backend habilitado/desabilitado e erro por SDK ausente; isso valida configuração,
**não** uma compilação Chromium. GPU e build Chromium não foram retomados.

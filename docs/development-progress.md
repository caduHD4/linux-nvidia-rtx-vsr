# SDD ledger — plan: docs/superpowers/plans/2026-10-05-chromium-nvidia-vsr.md

Authorization: user approved design and explicitly requested complete inline execution without further questions.
Pre-flight: Task 1 ProcessorConfig/CudaFrameView -> Task 4 GPU backend; matching explicit context and pitched RGBA8.
Pre-flight: Task 3 cache and Task 4 IPC share generation/frame ID/exported image lifetime; scheduler originals remain authoritative.
Pre-flight: Task 2 checkout -> Tasks 3/4 modifications; exact revision fixed before patch creation.
Ruling: start on stable Chromium 154 release rather than main 157 — smaller API churn and matches installed Brave major — if wrong, adapters need rebase.
GPU revalidation: ultra 720p->1080p passed on RTX 4070 SUPER, 10.389 ms/frame.
Task 1: policy RED missing SelectProcessorConfig/ValidProcessorConfig definitions; GREEN CPU 4/4.
Task 1: processor GPU RED missing VsrProcessor symbols; GREEN mode4/mode11 output-channel/alpha/invalid-pitch/busy/drain checks.
Task 1: GPU CTest 6/6 passed; 10,000-frame stress running separately. Initial timings are not playback guarantees.
Ruling: checkout root is Builds/brave/src, not Builds/brave/chromium/src — gclient was configured at build root; no extra nested checkout required — cost if wrong: update tooling paths.

Task 1: complete — commit a7193a3; CPU 5/5 and core GPU 10,000 frames each mode.
Task 3 preparatory cache: RED undefined cache methods -> GREEN repeated refresh/late completion/stale generation/capacity. Chromium adapter still pending.
Task 4 standalone bridge: RED missing bridge implementation; initial GPU test default EGL selected llvmpipe and correctly failed CUDA GL 219. Explicit NVIDIA GLVND vendor plus EGL_PLATFORM=x11 selected RTX 4070 SUPER and passed mode4/mode11 GL->CUDA->VFX->GL 30 iterations each.
Ruling: pin NVIDIA EGL vendor for native interop diagnostic — default dispatch selected Mesa software — cost if wrong: explicit vendor may not generalize to another installation; test launcher scoped to this host.

Task 4 native GLES interop: explicit NVIDIA EGL GLES3 context passed both modes as well as desktop GL.
Ruling: standalone bridge validation runs before Chromium baseline completes — independent native-resource gate can be tested while depot_tools downloads — no claim that Chromium backing/thread/sandbox gate has passed.
Ruling: bootstrap infrastructure downloaded hundreds of MiB for >12 minutes; terminate only our parent orchestrator (not CIPD child) and prefetch pinned Chromium source directly with Git in parallel. Reuse the same src checkout for gclient dependencies — avoids duplicate checkout and tool-download serialization.
Native GL/GLES private texture sharing across two EGL contexts and a separate worker thread passed for both modes. Diagnostic owns original GL context on main test thread and initializes VFX/bridge only on worker.
Fetch investigation: v2 capability GET and narrow ls-refs POST return in <1 s. Packet trace confirms tag fetch reaches want-ref/deepen1/done; awaiting server pack generation, not initial API access. Keep one src checkout; do not duplicate.

Fetch unblocked: server generated shallow stable pack after ~8 minutes; src pack now downloading.
Processor instrumentation: RED first Drain left timing at zero; GREEN event elapsed time recorded after successful drain; full GPU/CPU CTest 9/9. Missing-runtime test now exercises a valid current CUDA context.

Task 2 source checkout completed on b510e9d7cd3a2fbd78d0ddc42234103206c5f78d, branch feature/linux-nvidia-vsr. gclient dependencies in progress. Checkout script now reuses an already verified commit instead of fetching a second time.
Task 3 metadata admission RED missing header -> GREEN protection-first, CPU, HDR/10-bit, geometry, alpha/color/above-target bypass; full suite 10/10. Chromium mapping/import tests pending.

Task 2 dependencies/hooks completed; GN 34,297 targets generated. Probe/cache Chromium object build underway. First full build required generated DAWN_VERSION; ran exact upstream metadata hooks and resumed selected objects.
Ruling: native EGL must use the existing validating command decoder, compile enable_validating_command_decoder=true — passthrough explicitly CHECKs ANGLE for WebGL validation — retain that security CHECK unchanged; cost: additional GPU objects/build time.
Core GN RED official SDK structs rejected by Chromium raw_ptr plugin because installed outside third_party. Add narrowly scoped SDK-path plugin exclusion, preserving proprietary ABI and checks on browser adapter.
Ruling: private input AND output GL staging with GPU publication copy is preferable to registering the renderer output SharedImage — prevents CUDA registrations outliving renderer leases — cost: one additional GPU copy. Source-release fence will cover staging copy only, not the entire VFX job, to prevent a late enhancement from blocking decoder recycling through SharedImageInterface.

Task 2 native decoder build configured: use_siso=false, enable_validating_command_decoder=true, enable_nvidia_vsr=true with external SDK root. Full chrome + media_unittests build started; reuse existing objects. All downloads/tool caches explicitly scoped to browser volume.
Core GN GREEN after SDK system-header treatment and path-only raw_ptr exclusion. Dimensions extracted from smoke_config.h into own header; GPU/CPU suite remains 10/10.
Pre-sandbox preload RED missing PreloadRuntime -> GREEN loads/version-checks modules before CUDA context initialization; retains handles until process exit, no effect or image created. Full core suite 10/10. Chromium sandbox permissions and actual SDK use remain pending.
Probe renderer/cache and their test objects compiled with bundled Chromium toolchain; corrected actual metadata header path in test. Full test executable not linked/run yet.
Synthetic fixture check found FFmpeg codec options alone emitted unspecified primaries/transfer. Adding setparams filter produced explicit BT.709 primaries/transfer/matrix; regenerating all five owned clips.

Task 4 admission policy RED missing job_admission -> GREEN two-job limit, single running job, 50ms queue expiry, >100ms watchdog disables admission without freeing textures, physical-completion-only release, quarantine retention; core CTest 11/11. Controller currently independent; wire into actual service before claiming integration coverage.
User reiterated continue during full build. Browser build ~53,453 incremental steps on Ryzen 5 5600X; no browser delivery claimed.

Ruling: acknowledge source-copy completion via nonblocking GL fence polling on the owning GPU sequence, independent of the single CUDA worker queue — model initialization or an earlier VFX job must not delay decoder-surface release/SII dependencies — use separate owner/worker GL fences at the same submitted point to avoid concurrent wait/delete. Cost: one additional lightweight GL fence and polling task per admitted frame.
GPU worker source prepared in project overlay; not applied or compiled against browser yet. It receives only private native texture IDs; caches bridges, reinitializes effect on config change, skips inference after queued expiry, and conservatively retains all context/private resources after failures.

Ruling: VideoFrame::UpdateReleaseSyncToken explicitly requires ordering the previous release. A custom SyncTokenClient collects that previous token as a source-copy scheduler dependency and publishes the known source-copy token. This merges previous uses and the new copy without enqueuing a future-token wait into SharedImageInterface; output inference completion stays on an independent sequence. Receiver initialization must acknowledge both sync-point client states before this client is used.
Readonly channel-owned SharedImage metadata query prepared in overlay to reject protected resources before representation creation. Overlay remains unapplied while initial browser compilation runs.

User explicitly canceled work to upload to GitHub. Sent SIGINT to owned browser build process group 231170; Ninja confirmed interrupted at 13,270/53,453 steps. Browser has not been delivered or validated. Preserved current Chromium tracked/probe changes in chromium/patches/chromium-current-wip.patch; core is reproduced by stage-core.sh. New GPU service/client/synchronization work remains uncompiled in chromium/overlay. Source-release helper test reached expected missing-header RED; implementation compile then rejected inline virtual methods by chromium-style plugin (not fixed due cancellation). No completed browser claim.

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

### Continuidade: queda no contexto GL e rejeições restantes

A troca YouTube 360p→480p expôs uma queda do processo GPU em
`SkiaGLImageRepresentation::CheckContext`: `PollSourceFence` e `FinishSource`
são tarefas separadas, e outro cliente pode mudar o contexto entre elas.
`FinishSource` agora restabelece o contexto antes de encerrar o acesso Skia.
Se isso falhar, conserva o acesso e o SourceToken com continuação suspensa,
sem liberar superfícies inseguras. Revisão confirmou que `ContinueTask` é
necessário antes de `DisableSequence` para reter o token.

Testes nativos após a correção: 2/2. A repetição de 45s em
`validation/youtube-switch-1791325475213712046/report.json` passou saúde da
GPU e seleção aprimorada, mas **falhou continuidade**: geração1 teve 118/120
na segunda janela, seguida de 120/120; geração2 teve todas as janelas
pós-aquecimento em120/120. A investigação continua; essa rodada não deve
ser apresentada como aprovação do gate de continuidade.

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

Binário final sem traces temporários: validation/vsr-1791332743690718141/report.json, playback20s1080p30→1440 passou continuidade,480 seleções. Chromium interativo reaberto com --restore-last-session e padrão1440.

Pedido do usuário: alvo padrão alterado de1440 para2160 (3840×2160), mantendo VSR_ULTRA4, strength1.0, sharpen0.35 e fullscreen-only. Presets1080/1440 continuam disponíveis. Sem execução de testes por instrução explícita; qualidade/desempenho4K serão avaliados pelo usuário.


## 2026-10-07 — Brave port built and playback validated

User authorized autonomous delivery of a functional Brave port and optional
automatic upstream updates. Official Brave v1.96.61 pins Chromium
154.0.8037.98 (b859317bf11f6be47f9b7799ec690a0a42a1fb33); compared with the
tested .97 pin, tracked Chromium source differs only in chrome/VERSION.
A separate source tree under /home/caduhd4/Builds/brave/port preserves the
working Chromium output and all original user profiles. Official Brave patches
were applied first, then the exported VSR delta passed clean application.

The port has pinned bootstrap/apply/build/run tools, distinct package/install
identity and playback-check browser selection. Python regression suite:46/46
passed. Brave compiled successfully (2,626 steps, 23m06s) to
`/home/caduhd4/Builds/brave/port/src/out/BraveVsr/brave`; the executable
reports `Brave Browser Development 154.1.96.0`. A local H.264 1080p30 HTML5
fullscreen playback on RTX 4070 SUPER / driver 615.71.09 decoded 1920x1080
and selected 480 enhanced frames after warmup. Consecutive post-warmup windows
reported 120/120 enhanced selections without original frames or switches.
After exiting fullscreen, 178 additional frames decoded with no new enhanced
selection events. This validates the Brave binary and fullscreen-only VSR path.

The GPU process reported `sandboxed=false`; inference with the GPU sandbox
active remains unvalidated. Fullscreen-exit sampling is based on selection log
events every120 frames, not exact per-frame output measurement. The pinned
Ninja redirect_cc bootstrap fails against patched base headers, so builds use
the official default Siso path. Cargo requires libcurl-gnutls on CachyOS; the
local build used an independently extracted, signature-verified CachyOS
package under external tools/. No SDK or build artifacts are included in Git.

Automatic hosted GitHub delivery is omitted as permitted by the user: standard
runner resources, six-hour jobs and external SDK/RTX validation requirements
do not support a reliable turnkey setup. See docs/brave.md for launch/build
instructions and the sandbox limitation.

## 2026-10-07 — Brave performance build approved interactively

Rebuilt Brave with `dcheck_always_on=false` and
`enable_expensive_dchecks=false`, retaining the component build, native EGL,
validating command decoder and the existing fullscreen-only 2160 VSR path.
Local Siso build completed successfully in 3h12m38s (27,626 steps).
The user tested this binary and reported smooth browser interaction. This is
subjective acceptance, not a controlled performance benchmark; no PGO,
non-component build or passthrough decoder improvement is claimed.
Publication regression check: Python 48/48 passed. NVIDIA SDK and build outputs
remain external.

## 2026-10-07 — Brave baseline release and native quality settings

Published `brave-v0.1.0-preview.1` at project commit c797aee, preserving the
user-approved browser before quality-setting changes. The 389,598,225-byte
archive and SHA256 asset are uploaded. Package prerequisite check passed;
relocated 1080p30→4K playback selected 480 enhanced frames with continuous
post-warmup windows, and fullscreen exit decoded another 180 frames with zero
new enhanced-selection events. SDK and profiles were excluded.

Added Linux Settings > System selector Off/1080p/1440p/4K. Local-state preference
is snapshotted once for the session and propagated identically to renderer and
GPU. Apply via the existing Restart Brave action. First-launch environment
preset initializes the preference; later saved UI settings take precedence.
Off bypasses before renderer GPU connection and independently at GPU initialize.
Explicit invalid targets bypass. The existing fullscreen, protected/HDR and
1080p-source restrictions remain.

Initial incremental build: 27 steps / 48.02s. Startup testing exposed an early
zygote callback reading browser prefs before initialization; restricting preference
access to initialized renderer/GPU launches fixed it (3 steps / 23.29s).
Final Off-gate refinement and UI resource build: 13 steps / 25.37s.
Native UI/persistence tests exercised 2160→1440→0→2160 across fresh launches;
renderer and GPU switches matched the prior session snapshot even after changing
the saved preference. Core explicit-target tests and Python 50/50 passed.
Playback checks passed Off (no enhanced selections) and 1080/1440/2160, each
enabled mode selecting 480 enhanced frames with continuity and fullscreen exit.
A separate code review found the baseline-checkout migration gap; a simulated
full-patch-verified atomic upgrade now handles it, with corrupt-base no-mutation
regressions and a real published-baseline migration check. Follow-up review
reported no important remaining finding. The log terminal remains open.

GPU sandbox inference remains unvalidated; the published baseline discloses
the inactive GPU sandbox in its tested working mode. No security gate is claimed.

## 2026-10-07 — Native sharpness, denoise and restore defaults

Added integer post-sharpening 0–100 (default 35; zero skips the extra pass),
native-resolution denoise Low/Medium/High/Ultra (SDK modes 8–11; default 11),
and Restore defaults (2160/35/11) to Settings > System. Controls retain restart
semantics and share the per-session child snapshot. Denoise is explicitly
native-resolution-only: upscaling remains VSR Ultra, strength 1.0, without an
extra denoise pass. Restore saves explicit defaults and reloads actual preference
values after a partial/error save; restart is disabled during saving.

ProcessorConfig carries validated explicit sharpness; -1 preserves the older
environment path. GPU worker reuse compares sharpness too. Full patch plus
verified migration candidates upgrade both published c797aee and selector
1fa419a trees; both real-tree migrations and pristine/reverse checks passed.
Core configuration tests and Python 51/51 passed. Initial build checks exposed
TypeScript declaration-merge/private-helper and Lit click-handler naming errors;
fixed both. Final incremental build passed in 42.84s / 14 steps.

UI automation saved 1080/65/Low, restarted, restored 2160/35/Ultra, restarted
again, and checked renderer/GPU snapshot switches each session. Native GPU
playback confirmed mode8 1920x1080 output with post_sharpen0.65; 4K playback
confirmed mode4 3840x2160 with post_sharpen0.00. Each selected 480 enhanced frames
and passed post-warmup continuity plus fullscreen-exit checks.
Reports: validation/brave-image-ui-report.json and brave-image-playback-summary.json
on the build host (not committed). Independent review found no important issue;
its minor slider-step inconsistency was fixed by using step1. The first binary
release remains unchanged, and GPU sandbox inference remains unvalidated.


## 2026-10-07 — Native denoise for higher-resolution sources

Browser eligibility now accepts SDR sources through 4096x2160. Inputs above
1920x1080 denoise at their exact native dimensions using the selected mode and
sharpness, independently of upscale target. Off and invalid settings bypass;
protected/HDR/format/geometry gates and watchdog are unchanged. Generic frame
classification retains its original default cap.

Configuration and eligibility CPU tests passed, Python 51/51 passed, and the
incremental Brave build succeeded (13 steps, 12.08s). Complete/reverse patch
checks and reconstructed c797aee, 1fa419a and 3a23bc7 migrations passed. Independent
review found no important issue.

Isolated RTX 4070 SUPER playback with target1080, Ultra denoise and sharpness35
confirmed native 2560x1440 at 30fps and 3840x2160 at 24fps, post-warmup continuity,
zero player-reported dropped frames, and enhancement stopping on fullscreen exit.
4K GPU timings were about 38–41ms/frame; this is not a 4K60 guarantee. Initial
runs with another experimental browser using the GPU failed continuity; closing
that competing session restored it without pipeline changes. Reports on host:
port/validation/vsr-1791417601625989132 and vsr-1791417641246050746.
The first published binary preview remains unchanged; these changes are source
and local-build updates.

1080p30 → 4K upscale regression also passed continuity and fullscreen exit
(480 selected enhanced frames): port/validation/vsr-1791417679579979250.


## 2026-10-07 — Fixed Low denoise above 1080p and simplified settings

Following on-host comparison of native 4K Low (~44% GPU, 48 W) and Ultra
(~94% GPU, 213 W), the user chose fixed Low denoise above 1080p and removal of
the denoise selector. Sources above 1920x1080 through 4096x2160 now force SDK
mode 8 in core selection, independently of the previous denoise preference.
Brave no longer registers, exposes or reads that preference. Native processing
at or below 1080p remains Ultra; upscaling remains VSR Ultra. The settings panel
retains target, sharpness and Restore defaults (2160/35), with restart semantics.

The new core policy test failed before implementation and passed afterward.
Python 51/51 passed. Complete/reverse patch checks and migrations from c797aee,
1fa419a, 3a23bc7 and 6889b9a passed. Independent review found no important issue.
UI/resource build passed in 47.31s / 18 steps; the final allowlist update passed
in 7.43s / 3 steps. A new installable preview.2 is prepared separately from the
unchanged preview.1; runtime/package evidence follows after validation.

UI automation passed selector absence, target/sharpness save and reset across
relaunches, plus renderer/GPU consistency with an old Low preference present.
Report: validation/brave-low-denoise-ui-report.json on the build host.


Preview.2 package validation passed through the real per-user installer and
installed launcher in a relocated path containing spaces. Confirmed native 4K
Low (mode 8), 1080p→4K VSR (mode 4), and 1080p-native Ultra (mode 11), with 360/480/480
selected enhanced frames respectively. All three passed post-warmup continuity,
fullscreen exit and clean shutdown. Player-reported dropped frames were 0/0/3
respectively over each complete run; the last case still passed continuity.
Checksums, archive paths and SDK/profile exclusions passed. Package references
clean source a242614. Archive 365646283 bytes; SHA256 is shipped alongside it.
Reports: validation/brave-preview2-installed-summary.json on the build host.

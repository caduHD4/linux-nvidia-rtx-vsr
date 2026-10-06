# Build Chromium experimental

Checkout: `/home/caduhd4/Builds/brave/src`, branch local `feature/linux-nvidia-vsr`.
Base: Chromium **154.0.8037.97**, commit `b510e9d7cd3a2fbd78d0ddc42234103206c5f78d`.
SDK externo: `/home/caduhd4/Downloads/linux-nvidia/.sdk/VideoFX`.

`tools/chromium/checkout.sh` prepara depot_tools, checkout raso Linux e hooks.
`tools/chromium/stage-core.sh` copia somente os headers/fontes deste projeto
para o alvo GN `third_party/nvidia_vsr`; nenhuma biblioteca/modelo NVIDIA é copiada.

O output de desenvolvimento é `out/Vsr`: release, component build, símbolos
zerados, codecs proprietários habilitados. EGL NVIDIA nativo requer
`enable_validating_command_decoder=true` e `--use-cmd-decoder=validating`.
A validação ANGLE exigida pelo decoder passthrough permanece intacta.

Os caminhos de cache CIPD/vpython ficam em `Builds/brave/cache`. Manter pelo
menos 20 GiB livres. O script de build verifica essa reserva antes de começar;
monitorar também durante a compilação.

Logs locais: `chromium-checkout.log`, `chromium-gn.log`,
`chromium-build.log`, `chromium-core-build.log` e `chromium-probe-build.log`.
Os arquivos gerados de mídia ficam em `Builds/brave/media`; o gerador inclui
padrões sintéticos H.264/VP9/AV1 e metadados BT.709 explícitos.

Estado na transferência: build inicial/probe interrompido pelo usuário.
Para setup portátil, patches, overlay e erros conhecidos, leia
[CLOUD_HANDOFF.md](CLOUD_HANDOFF.md). Não demonstra VSR
em reprodução HTML5 nem validação do SDK dentro do sandbox. O comando final
de reprodução será registrado somente depois desses testes.

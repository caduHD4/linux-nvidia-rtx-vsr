# NVIDIA RTX VSR on Linux

Integração experimental do NVIDIA RTX Video Super Resolution no Chromium Linux,
com porte para Brave planejado. O Chromium modificado já reproduziu vídeo com
VSR no host de desenvolvimento (CachyOS/Wayland, RTX 4070 SUPER). O usuário
também avaliou o modo 4K localmente. Isso não comprova compatibilidade com outras
máquinas; o pacote pré-compilado para instalação pública ainda não está pronto.

Para continuar na nuvem, leia **[CLOUD_HANDOFF.md](docs/CLOUD_HANDOFF.md)**: contém
setup, revisão fixada, SDK externo, aplicação dos patches, estado dos testes,
erros conhecidos e próximos passos. Leia também o
[design](docs/specs/2026-10-05-chromium-nvidia-vsr-design.md) e o
[plano](docs/superpowers/plans/2026-10-05-chromium-nvidia-vsr.md).

O repositório contém fontes, testes, scripts e patches, sem checkout Chromium,
SDK proprietário, binários, caches ou vídeos gerados.

## Instalar as dependências NVIDIA

O SDK é baixado **diretamente da NVIDIA**. Não incluímos suas bibliotecas,
headers, modelos ou credenciais neste repositório. As instruções abaixo instalam
o SDK; não instalam um navegador pré-compilado nem substituem seu navegador atual.

### 1. Conta e acesso ao download

Entre no [NGC](https://ngc.nvidia.com/) com sua conta NVIDIA. Abra a página oficial
do [Video Effects SDK Core](https://catalog.ngc.nvidia.com/orgs/nvidia/maxine/resources/vfx_sdk_core/-)
e selecione a versão **1.3.0.0_linux**. Baixe o pacote Linux x86_64 indicado
nessa versão. Não use o SDK Windows, uma versão antiga de Maxine ou apenas os
samples do GitHub.

O catálogo pode pedir ingresso no NVIDIA Developer Program ou acesso por NVIDIA
AI Enterprise. Siga o acesso oferecido à sua conta e leia os termos correspondentes;
uma conta gratuita, por si só, não garante acesso a todos os artefatos. Não é
necessário comprar uma assinatura sem antes verificar a opção Developer Program
disponível na página. Este projeto não fornece nem compartilha acesso NGC.

### 2. Extrair o SDK

Instale `tar`, `xz`, `curl` e tenha o driver NVIDIA funcionando (`nvidia-smi`).
Execute os exemplos em **Bash**. Extraia o pacote em um diretório seu, sem `sudo`:

```bash
mkdir -p "$HOME/.local/opt/nvidia-vfx"
# Troque pelo caminho real do pacote baixado na etapa anterior.
SDK_ARCHIVE="$HOME/Downloads/VFXSDK_linux_1.3.0.0.tgz"
tar -xf "$SDK_ARCHIVE" -C "$HOME/.local/opt/nvidia-vfx"
export VFXSDK_ROOT="$HOME/.local/opt/nvidia-vfx/VideoFX"
test -f "$VFXSDK_ROOT/include/nvVideoEffects.h"
test -f "$VFXSDK_ROOT/features/install_feature.sh"
```

O nome do arquivo pode variar. `VFXSDK_ROOT` deve apontar para a pasta `VideoFX`
que contém `include`, `lib`, `external` e `features`, não para a pasta acima dela.
Se os comandos `test` falharem, confira onde o arquivo foi extraído antes de seguir.
O pacote Core já fornece dependências de runtime; preserve sua estrutura.

### 3. Criar uma chave NGC e instalar Video Super Resolution

Em **Account Settings → API Keys**, gere uma Personal API Key com acesso ao
**NGC Catalog**, conforme o [guia oficial de chaves NGC](https://docs.nvidia.com/ngc/latest/ngc-user-guide.html#generating-a-personal-api-key).
A chave precisa acessar os artefatos Maxine da sua conta. Informe-a no terminal
sem colocá-la no histórico do Bash:

```bash
read -r -s -p 'Cole sua chave NGC: ' NGC_CLI_API_KEY
printf '\n'
export NGC_CLI_API_KEY
(
  cd "$VFXSDK_ROOT/features" || exit 1
  bash ./install_feature.sh -f nvvfxvideosuperres -v 1.3.0.0
)
unset NGC_CLI_API_KEY
```

O script oficial detecta a arquitetura da GPU e baixa a feature e eventuais
modelos correspondentes. Como o SDK foi extraído em uma pasta sua, não use
`sudo`. Não publique a chave em issues, capturas de tela, logs ou arquivos Git.

Se houver falha de detecção, consulte `bash ./install_feature.sh --help` dentro
de `features`. O host validado usa compute capability **8.9**. Não escolha uma
arquitetura diferente apenas para forçar o download; o funcionamento em outras
GPUs ainda precisa ser verificado.

### 4. Conferir e configurar o projeto

```bash
test -f "$VFXSDK_ROOT/lib/libVideoFX.so"
test -f "$VFXSDK_ROOT/lib/libNVCVImage.so"
test -f "$VFXSDK_ROOT/external/cuda/lib/libcudart.so.12"
test -f "$VFXSDK_ROOT/features/nvvfxvideosuperres/include/nvVFXVideoSuperRes.h"
test -f "$VFXSDK_ROOT/features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so"
```

Esses comandos conferem arquivos, não comprovam inferência ou qualidade da imagem.
Para compilar este projeto, use o mesmo `VFXSDK_ROOT` no CMake (exemplo abaixo)
ou em `tools/chromium/build.sh`. O backend exige Core e feature **1.3.0.0**.

**Limitação atual do navegador:** o caminho do SDK é incorporado na compilação
Chromium por `nvidia_vsr_sdk_root`. Exportar `VFXSDK_ROOT` ao abrir um binário já
compilado não muda esse caminho. Não copie o executável da máquina de
desenvolvimento esperando que encontre seu SDK. A distribuição portátil ainda
precisa resolver isso; consulte o [setup e build](docs/CLOUD_HANDOFF.md).

O host local tem presets `1080`, `1440` e `2160` (4K), processamento somente em
tela cheia e sharpen padrão `0.35`; conteúdo protegido e HDR fazem bypass.
Esses avanços locais ainda não estão todos publicados na branch `main` e não
constituem uma Release instalável. Brave ainda não foi portado. O diagnóstico
local do Chromium informou GPU `sandboxed=false`; a distribuição pública do
navegador continua pendente da revisão de segurança e portabilidade.

### Problemas de instalação

| Sintoma | O que conferir |
| --- | --- |
| HTTP 401 | Chave ausente, inválida ou expirada. Gere uma chave adequada e tente novamente. |
| HTTP 402/403 ou falta de permissão | Acesso da conta, serviço da chave e termos do artefato no NGC. Não garante que seja obrigatório pagar. |
| HTTP 404 | Nome do recurso, versão Linux e disponibilidade no catálogo oficial. Não substitua silenciosamente a versão exigida. |
| `Driver/library version mismatch` | Reinicie após atualizar os pacotes NVIDIA e confirme `nvidia-smi` antes de investigar VFX. |
| Biblioteca ausente | Confira a extração completa e o caminho `VideoFX`; preserve `external/cuda`, `external/tensorrt`, `lib` e `features`. |
| Player funciona sem aprimoramento | Confira tela cheia, GPU/driver, conteúdo suportado e logs; reprodução normal pode ser fallback. |

### Licença e fontes oficiais

Pesquisa revisada em **2026-10-06**. A página da feature informa disponibilidade
para uso comercial e não comercial, mas isso não significa redistribuição
irrestrita. Os termos de AI Products preveem distribuição como parte de um
produto sob condições; a seção **F — NVIDIA Developer Programs** restringe
software Enterprise obtido sob licença pessoal do programa a avaliação,
desenvolvimento e testes internos. Ainda não confirmamos qual modalidade cobre
os artefatos instalados no host original. Assim, adotamos download separado;
isso não declara que todo SDK NVIDIA seja proibido de redistribuir. O usuário
deve respeitar os termos da modalidade com que obtiver o SDK.

- [Feature Video Super Resolution e termos aplicáveis](https://catalog.ngc.nvidia.com/orgs/nvidia/maxine/collections/nvvfxvideosuperres/-)
- [Instalação oficial Linux](https://docs.nvidia.com/maxine/vfx/latest/LinuxVFXSDK/InstalltheVFXSDK.html)
- [Requisitos e limites de suporte Linux](https://docs.nvidia.com/maxine/vfx/latest/LinuxVFXSDK/GetStartedonLinux.html)
- [NVIDIA Software License Agreement](https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-software-license-agreement/)
- [Product Specific Terms for NVIDIA AI Products, B e F](https://www.nvidia.com/en-us/agreements/enterprise-software/product-specific-terms-for-ai-products/)

A NVIDIA não oferece suporte oficial à integração local em aplicativos cliente
com esse SDK Linux. Nosso resultado local é experimental e não representa
endosso ou suporte da NVIDIA.

## Requirements

- NVIDIA RTX GPU and a working NVIDIA driver
- CMake 3.25 or newer and a C++20 compiler
- NVIDIA Video Effects SDK Core 1.3.0.0 for Linux
- `nvvfxvideosuperres` feature 1.3.0.0 matching the GPU compute capability

The SDK and feature are external NVIDIA/NGC prerequisites and are intentionally
not committed to this repository. After installing both under one root, point
CMake at that directory with `VFXSDK_ROOT`. The expected feature layout is:

```text
VideoFX/
├── include/{nvVideoEffects.h,nvCVImage.h}
├── lib/
└── features/nvvfxvideosuperres/
    ├── include/nvVFXVideoSuperRes.h
    └── lib/libnvVFXVideoSuperRes.so
```

## Build and run the GPU smoke test

```bash
cmake -S . -B build/gpu \
  -DCMAKE_BUILD_TYPE=Release \
  -DVFXSDK_ROOT="$VFXSDK_ROOT"
cmake --build build/gpu --parallel 2
ctest --test-dir build/gpu -L gpu --output-on-failure
```

Run it directly to see the selected GPU and timing:

```bash
./build/gpu/vsr-smoke \
  --quality high \
  --input 1920x1080 \
  --output 3840x2160
```

The named qualities map to NVIDIA VSR modes `low=1`, `medium=2`, `high=3`, and
`ultra=4`. Use `./build/gpu/vsr-smoke --help` for all options.

## CPU-only development tests

The parser, resource lifecycle, failure cleanup, and SDK discovery tests can run
without loading the NVIDIA SDK:

```bash
cmake -S . -B build/cpu -DNVVFX_VSR_BUILD_SMOKE=OFF
cmake --build build/cpu
ctest --test-dir build/cpu --output-on-failure
python -m unittest discover -s tests/python -v
```

If `nvidia-smi` reports `Driver/library version mismatch`, reboot into the
kernel installed with the current NVIDIA packages before diagnosing VFX.

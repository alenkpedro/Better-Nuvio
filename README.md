# Better Nuvio

Cliente de streaming para LG webOS, com prévias para macOS e trabalho experimental em outras plataformas. Distribuído sob [GPL-3.0](LICENSE); os avisos de atribuição estão em [NOTICE.md](NOTICE.md).

## Instalar no LG webOS

Adicione este catálogo ao Homebrew Channel:

```text
https://betternuvio.vercel.app/apps.json
```

O [site de atualizações](https://betternuvio.vercel.app/) mostra as notas e o pacote IPK atual. Os metadados do catálogo ficam em `lab-catalog/`.

## Publicar uma atualização

O IPK não é enviado ao GitHub. Por isso, o `vercel.json` da raiz desativa a implantação automática a cada push: uma implantação feita apenas do código-fonte substituiria o catálogo e faria o botão **Atualizar agora** receber 404. Depois de compilar o IPK e atualizar os metadados em `lab-catalog/`, publique **a partir desse diretório** com `vercel deploy --prod --yes`. Confirme no domínio público que `/releases/latest.json` e o IPK anunciado pelo JSON respondem com sucesso, e compare o SHA-256 do download com o `digest` anunciado.

## Código-fonte

Este repositório é um snapshot limpo do código atual do Better Nuvio. Ele não contém o histórico Git local, chaves, tokens, contas, configurações pessoais, pacotes IPK ou binários gerados. O IPK 1.5.35 é publicado pelo catálogo. O segredo OAuth do Trakt fica apenas no ambiente do servidor; o Seekr usa uma chave pessoal configurada no dispositivo.

Os componentes principais estão em `src/` (cliente nativo), `deploy/app/` (recursos), `plugin-service/` (executor local), `tools/` (build) e `tests/`. Para compilar no Mac, use `bash tools/mac.sh`; para LG, `bash tools/arm.sh --ipk`. Consulte [as notas de compilação](docs/ORIGINAL_BUILD_NOTES.md) para dependências e variáveis locais. Os arquivos de conta são criados localmente e não são distribuídos.

O player precisa dos arquivos locais `NetflixSans-Regular.otf` e `NetflixSans-Medium.otf` em `deploy/app/fonts` para manter a tipografia das legendas. Eles não fazem parte do repositório; os scripts de compilação recusam um pacote sem esses arquivos, evitando substituição silenciosa da fonte.

## Créditos

Agradecimento especial a **[iqui27](https://github.com/iqui27)**, criador do **[Nuvio Native Legacy](https://github.com/iqui27/nuvio-native-legacy)**, pelo trabalho no cliente nativo para TVs e pelo código incorporado ao Better Nuvio. O projeto combina essas contribuições com interface, integrações e adaptações de plataforma próprias.

Créditos também à **[NuvioMedia](https://github.com/NuvioMedia)** pelo Nuvio e seu ecossistema original.

Os avisos de atribuição estão em [NOTICE.md](NOTICE.md). Better Nuvio é um projeto não oficial, sem afiliação com NuvioMedia. Consulte também as licenças de bibliotecas e fontes nos respectivos diretórios.

## Motores refeitos

A reconstrução de legendas e continuar assistindo está documentada em [ENGINE-REBUILD.md](ENGINE-REBUILD.md). APK 0.1.27 e IPK 1.5.35 usam o mesmo núcleo novo.

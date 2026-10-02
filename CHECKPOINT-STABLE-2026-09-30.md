# Checkpoint estável — 30 de setembro de 2026

## Validação

Estado aprovado pelo usuário em 30/09/2026 (America/Bahia):

> Marque um "checkpoint" importante aqui, tudo tá funcionando bem redondo

Este é o ponto de referência para as próximas alterações do Better Nuvio.

## Código no GitHub

- Repositório: `alenkpedro/Better-Nuvio`.
- Tag: `checkpoint-estavel-2026-09-30`.
- Commit: `087fe4f8ac4f308615b1527b28b295161a224409`.
- Branch: `codex/rebuild-engines-2026-09-30`.
- PR: <https://github.com/alenkpedro/Better-Nuvio/pull/2>.
- Registro: <https://github.com/alenkpedro/Better-Nuvio/releases/tag/checkpoint-estavel-2026-09-30>.

A tag aponta para o código compartilhado da versão validada. O projeto Android específico está na pasta local `Better Nuvio Android TV`; seus binários publicados estão preservados nos anexos do checkpoint. A PR permanece aberta e o checkpoint não representa um merge na branch principal.

## Versões preservadas

| Plataforma | Versão | Arquivo | Tamanho em bytes |
| --- | --- | --- | --- |
| LG webOS / Homebrew | 1.5.37 | `com.betternuvio.app_1.5.37_arm.ipk` | 52085706 |
| Android TV / Fire TV | 0.1.29 (versionCode 30) | `better-nuvio-android-tv-0.1.29-firetv-4k-max-2-debug.apk` | 39031056 |

SHA-256 do IPK:

```text
19ab2dd9e0a33ff22d5f5af053cdaca66190a8d5cefcf51633c9f114e49fcbf4
```

SHA-256 do APK:

```text
d8418db0e9ecff3aea30acd0759857c5aabbef53e60b0e9fa1de3e9efda61fee
```

Os arquivos do catálogo local foram conferidos por tamanho e SHA-256 antes de registrar o checkpoint. A publicação no Vercel também havia sido conferida com esses mesmos valores.

- Catálogo: <https://betternuvio.vercel.app/>.
- Homebrew: <https://betternuvio.vercel.app/apps.json>.
- IPK: <https://betternuvio.vercel.app/com.betternuvio.app_1.5.37_arm.ipk>.
- APK: <https://betternuvio.vercel.app/better-nuvio-android-tv-0.1.29-firetv-4k-max-2-debug.apk>.

## Funcionalidades desta base

- Motor de legendas reconstruído usando o Nuvio oficial e o Enhanced como referência.
- Menu de legendas com nome do addon, informação original da legenda e recomendação por arquivo, por idioma.
- Faixas de áudio e troca de faixa no teste do Mac.
- Continuar assistindo completo, expandindo em grupos de 12 títulos.
- Identidade, capa e progresso do arco de Lizzie Borden preservados.
- Navegação superior com transparência e animação; removida a sombra rígida abaixo da cápsula.
- Correções anteriores de inicialização no Fire TV e de addons sem fontes incluídas.

## Retomar este estado

Para comparar ou recuperar o código compartilhado, use a tag `checkpoint-estavel-2026-09-30` em um checkout separado. Para reinstalar as versões validadas, use os instaladores anexados ao registro do GitHub e confira os hashes acima.

A validação do Mac corresponde ao aplicativo de teste aberto a partir desta base. Este registro não inclui dados da conta, credenciais nem histórico de reprodução do usuário.

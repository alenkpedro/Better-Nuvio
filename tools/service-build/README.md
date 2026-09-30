# Serviço webOS

Instale as dependências de compilação com `npm ci --prefix tools/service-build`.
`node tools/service-build.cjs` gera `plugin-service/runtime/service.cjs`; o IPK
e o teste Mac executam esse bundle, sem depender de npm na TV.

O bootstrap inclui polyfills e é compilado para Node 0.12.2 (webOS 3/4),
mantendo suporte a Node 8 (webOS 5/6) e versões posteriores. O extrator oficial
mantém seu algoritmo; apenas a chamada HTTP usa o overload `request(options,
callback)` disponível nesses runtimes. A inicialização de QuickJS fica restrita
a pedidos de plugins e não impede a extração de legendas em runtimes sem WASM.

Verificação do pacote, com arquivos MKV reais:

```sh
bash tests/subtitle_service_legacy.sh
NUVIO_SERVICE_NODE_IMAGE=node:8.17.0-alpine bash tests/subtitle_service_legacy.sh
```

Para Node 0.12.2, use um container Linux com o binário oficial dessa versão e
informe seu caminho em `NUVIO_SERVICE_NODE`. O teste usa somente sintaxe ES5.
Ele cobre o `main` real de package.json, HTTP Range, redirect, 43 faixas, SRT,
ASS e recuperação de erro sem derrubar o serviço.

Referência de versões de Node da LG:
https://webostv.developer.lge.com/develop/guides/js-service-basics

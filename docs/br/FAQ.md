# FAQ — dúvidas frequentes

## O que você pode usar

- Qualquer linguagem, framework, banco de dados ou tecnologia de sua preferência.
- Bancos vetoriais dentro do seu `docker-compose.yml` (pgvector, Qdrant, SQLite-vss e similares).
- Pré-processamento do dataset durante o build do container ou no startup.
- Qualquer técnica de classificação — KNN exato, ANN aproximado, ou outro método que você queira experimentar.

Se você quer colaborar com a edição (sugerir melhorias, reportar problemas, corrigir algo na documentação), fique à vontade para abrir um pull request. A Rinha é mantida por uma pessoa só, e correções são bem-vindas.

## O que não é permitido

- Usar um load balancer que aplique qualquer lógica relacionada à detecção de fraude. O load balancer só distribui requisições.
- "Esconder" a sua submissão para que outras pessoas não possam aprender com ela. A ideia da Rinha é o aprendizado coletivo.
- Desrespeitar outros participantes ou quem organiza a Rinha. Ninguém está ganhando dinheiro com isso — a proposta é aprender e se divertir.
- Usar os payloads do teste como referência ou para fazer lookup de fraudes!

## Erros comuns

- **Imagem docker não pública.** As imagens referenciadas no seu `docker-compose.yml` precisam estar acessíveis publicamente para que o ambiente de teste consiga baixá-las.
- **Imagem compatível apenas com arm64.** Se você usa Mac com chip Apple, lembre-se de construir uma imagem compatível com `linux/amd64`. O ambiente de teste roda em um Mac Mini Late 2014 com arquitetura amd64.
- **Repositório git não público.** O seu repositório precisa estar público para que a submissão possa ser avaliada e para que outras pessoas possam aprender com o seu código.

---

[← voltar ao README](./README.md)

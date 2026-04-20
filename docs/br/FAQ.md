# FAQ — Dúvidas Frequentes

## O que pode e o que não pode

> TODO: lista de regras / proibições / liberações.

**Pode:**
- Usar qualquer linguagem / framework
- Usar bancos vetoriais (pgvector, Qdrant, SQLite-vss, etc.) dentro do seu compose
- Pré-processar o dataset no build do container
- Usar KNN exato, ANN aproximado, ou qualquer outro classificador

**Não pode:**
- *TODO: definir restrições claras (sem cache externo, sem acesso à rede durante o teste, etc)*

## A API precisa ser exatamente KNN?

> TODO: esclarecer posição final (KNN como referência vs. liberdade total de algoritmo).

## Posso pré-calcular / comprimir o dataset?

> TODO.

## Posso usar modelos de ML treinados (XGBoost, redes neurais)?

> TODO.

## O que acontece se meu `fraud_score` não bater com a referência?

> TODO: esclarecer que só `approved` é avaliado, `fraud_score` é informativo.

## Quanto tempo o dataset leva para carregar?

> TODO: considerações sobre warmup / `/ready`.

## Erros comuns

> TODO: armadilhas observadas em edições anteriores ou esperadas nesta.

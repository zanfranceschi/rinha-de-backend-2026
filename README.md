# Rinha de Backend 2026

## O Desafio

Você foi contratada/o para construir um módulo de **Detectção de Fraude** que será integrado a um sistema completo de aprovação de transações financeiras!

```mermaid
flowchart LR
    Client[Cliente] -->|1. transação| Sistema[Sistema de Aprovação<br/>de Transações]
    Sistema -->|2. consulta| Fraude[Módulo de<br/>Detecção de Fraude]
    Fraude -->|3. decisão + score| Sistema
    Sistema -->|4. aprovada/negada| Client

    classDef highlight fill:#4ade80,stroke:#15803d,stroke-width:3px,color:#000,font-weight:bold
    class Fraude highlight
```

Seu módulo deverá detectar fraudes se baseando em um dataset de referência [(*references.json.gz*)](./resources/) com 100k registros usando busca vetorial!

**Não se assuste! Busca vetorial é mais simples do que você imagina caso nunca tenha ouvido falar disso antes!**

### Busca Vetorial / Dataset de Referência
O arquivo [references.json](./resources/references.json.gz) possui um formato semelhante ao seguinte:
```json
[
  { "vector": [0.0100, 0.0833, 0.05], "label": "legit" },
  { "vector": [0.5796, 0.9167, 1.00], "label": "fraud" },
  { "vector": [0.0035, 0.1667, 0.05], "label": "legit" },
  { "vector": [0.9708, 1.0000, 1.00], "label": "fraud" },
  { "vector": [0.4082, 1.0000, 1.00], "label": "fraud" },
  { "vector": [0.0092, 0.0833, 0.05], "label": "legit" }
]
```
*Obs.: Esse exemplo possui vetores de 3 dimensões; o original possui 14 dimensões. Todas as explicações a seguir se aplicam igualmente a vetores de quaisquer dimensões.*

Os vetores representam as variadas dimensões de uma transação e se ela é classificada com fraudulenta ou legítima (`legit|fraud`).

Vamos imaginar que essas 3 dimensões representam o seguinte:
- Valor da Transação
- Hora do Dia
- Média do Valor de Transação do Portador

Para que os vetores não fiquem assim (com valores absolutos)...
```json
[
  { "vector": [12.00,    9,  23.97], "label": "legit" },
  { "vector": [12000.00, 23, 500.34], "label": "fraud" }
]
```
...nós os normalizamos para que fiquem entre 0 e 1. Assim, numa busca vetorial, um valor de 34.000,00 de um compra não distorce em relação a outros números tais como "hora do dia".

#### Mais que raios é uma busca vetorial???

Uma busca vetorial é simplesmente achar o que mais se parece em vez de buscar por igualdade exata. É uma busca por semelhança.

#### Um exemplo passo-a-passo

Vamos acompanhar uma transação do início ao fim — da chegada dos dados brutos até a decisão.

**1. Constantes de normalização**

Valores de referência que definem o "teto" de cada dimensão:
```json
{
  "max_amount": 10000,
  "max_hour":   23,
  "max_avg":    5000
}
```

**2. Chega uma nova transação (dados brutos)**

```
Valor da transação:            R$ 8.200,00
Hora do dia:                   22h
Média histórica do portador:   R$ 4.800,00
```

**3. Normalização — cada campo vira um número entre 0.0 e 1.0**

```
dim1 = 8200 / 10000 = 0.82
dim2 = 22   / 23    = 0.96
dim3 = 4800 / 5000  = 0.96
```

Vetor de consulta resultante:
```
[0.82, 0.96, 0.96]
```

**4. Busca vetorial — calcular a distância até cada referência**

Usando distância euclidiana (quanto menor, mais parecido):
```
dist(q, ref) = √( (q1-r1)² + (q2-r2)² + (q3-r3)² )
```

| # | vetor de referência       | label | distância até `[0.82, 0.96, 0.96]` |
|---|---------------------------|-------|-------------------------------------|
| 4 | [0.9708, 1.0000, 1.00]    | fraud | **0.161** ← mais perto              |
| 2 | [0.5796, 0.9167, 1.00]    | fraud | **0.247**                           |
| 5 | [0.4082, 1.0000, 1.00]    | fraud | **0.416**                           |
| 3 | [0.0035, 0.1667, 0.05]    | legit | 1.458                               |
| 1 | [0.0100, 0.0833, 0.05]    | legit | 1.501                               |
| 6 | [0.0092, 0.0833, 0.05]    | legit | 1.501                               |

**5. Os K=3 vizinhos mais próximos**

```
1º — ref 4 (fraud) — 0.161
2º — ref 2 (fraud) — 0.247
3º — ref 5 (fraud) — 0.416
```

**6. Votação por maioria**

```
fraud: 3 votos
legit: 0 votos

fraud_score = 3 / 3 = 1.0
```

**7. Decisão**

Com threshold `0.6`:
```
fraud_score (1.0) >= threshold (0.6) → transação NEGADA
```

Resposta:
```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

**A intuição:** os três vizinhos mais próximos têm em comum *valor alto, horário tardio e portador de alto padrão* — exatamente o "formato" de transações marcadas como fraude no dataset. A busca vetorial não "entende" fraude; ela só encontra as transações passadas mais parecidas e deixa a maioria decidir.




Nesta edição, o desafio é construir uma **API de detecção de fraude em transações financeiras** usando KNN (K-Nearest Neighbors).

A API recebe requisições de autorização de transações e precisa classificar cada uma como fraude ou legítima, retornando um score de fraude. A classificação é feita comparando a transação recebida contra um dataset de referência pré-carregado, usando distância por cosseno para encontrar os vizinhos mais próximos.

**Em resumo:**
1. A API recebe uma transação com vários campos (valor, parcelas, horário, distância de casa, etc.)
2. Normaliza esses campos em um vetor de 14 dimensões (valores entre 0.0 e 1.0)
3. Compara esse vetor contra os vetores do dataset de referência usando distância por cosseno
4. Pega os 5 vizinhos mais próximos (K=5) e faz votação por maioria
5. Retorna se a transação foi aprovada e o score de fraude

Não precisa de banco de dados. Não tem estado compartilhado entre instâncias. O dataset de referência é carregado na memória na inicialização.

Participantes podem usar qualquer tecnologia: brute force em memória, bancos vetoriais (pgvector, Qdrant, SQLite-vss...), índices espaciais, ou qualquer outra abordagem.


## KNN e Vetores — O Básico

### O que é um vetor?

Um array de números que descreve uma "coisa". Por exemplo, um vinho poderia ser descrito assim:

```
          [preço, doçura, amargor, acidez, corpo]
Malbec  = [0.7,   0.2,    0.6,     0.5,    0.9  ]
Moscato = [0.3,   0.9,    0.1,     0.3,    0.2  ]
```

Vinhos com características parecidas têm vetores parecidos — ficam "perto" um do outro.

Na detecção de fraude, cada transação vira um vetor que descreve o "formato" dela — valor, horário, distância de casa, etc.

### O que é KNN?

K-Nearest Neighbors. Você tem um monte de exemplos rotulados (fraude ou legítimo). Uma nova transação chega. Você encontra os **K exemplos mais próximos** e deixa eles votarem.

```
K = quantos vizinhos perguntar

Dados de referência:
  [0.9, 0.85, ...] fraude
  [0.1, 0.20, ...] legítimo
  [0.8, 0.90, ...] fraude
  [0.2, 0.15, ...] legítimo
  [0.7, 0.80, ...] fraude

Vetor da nova transação: [0.85, 0.88, ...]

5 vizinhos mais próximos: 3 fraude, 2 legítimo
fraud_score = 3/5 = 0.60
Resultado: fraude (votação por maioria)
```

Esse é o algoritmo inteiro. Sem treinamento, sem modelo — só "encontre as coisas mais parecidas que você já viu e seja o que a maioria delas é".

### O que é normalização?

Colocar todos os números na mesma escala (0.0 a 1.0) para que sejam comparáveis.

Sem normalização, as dimensões ficam completamente desproporcionais:

```
valor:      R$ 0,50  a  R$ 50.000
hora:       0        a  24
distância:  0 km     a  20.000 km
```

Se você calcular distância com os valores brutos, o **valor domina tudo** só porque os números são maiores. Uma diferença de R$ 20 pesaria mais que uma diferença de 12 horas.

A normalização comprime tudo para 0.0-1.0 para que cada dimensão tenha peso justo no cálculo de distância.

**Observação:** os valores normalizados devem estar sempre entre 0.0 e 1.0. Se um valor bruto ultrapassar a constante de normalização, ele deve ser limitado (clamped) a 1.0.

Exemplo:
```
max_amount = 10000

amount = 8000   --> 8000 / 10000  = 0.80  (ok)
amount = 15000  --> 15000 / 10000 = 1.50  --> clamp para 1.0
amount = 0      --> 0 / 10000     = 0.00  (ok)
```

### O valor sentinela

Quando `last_transaction` é `null` (sem dados da transação anterior), duas dimensões perdem a fonte de dados. Em vez de chutar um default, usamos `-1` — um valor claramente fora do intervalo 0.0-1.0, significando "dado não disponível". O dataset de referência usa a mesma convenção, então o KNN naturalmente agrupa transações "sem histórico" com outras transações "sem histórico".


## API

### `POST /fraud-score`

**Request:**
```json
{
    "id": "tx-1899203618",
    "transaction": {
        "amount": 26.27,
        "installments": 1,
        "requested_at": "2026-03-13T12:01:12Z"
    },
    "customer": {
        "avg_amount": 52.54,
        "tx_count_24h": 4,
        "known_merchants": ["MERC-016", "MERC-018", "MERC-013"]
    },
    "merchant": {
        "id": "MERC-018",
        "mcc": "5912",
        "avg_amount": 320.38
    },
    "terminal": {
        "is_online": false,
        "card_present": true,
        "km_from_home": 32.04
    },
    "last_transaction": {
        "timestamp": "2026-03-14T04:29:53Z",
        "km_from_current": 3.81
    }
}
```

> `last_transaction` pode ser `null`.

**Response:**
```json
{
    "approved": true,
    "fraud_score": 0.0
}
```

- `approved`: `true` se a transação deve ser aprovada, `false` caso contrário.
- `fraud_score`: score de fraude entre `0.0` (legítima) e `1.0` (fraude).

O HTTP status é sempre `200` para requisições válidas.

### `GET /ready`

Health check. Deve retornar `200` quando a API estiver pronta para receber requisições.


## Dimensões do Vetor

O payload é convertido em um vetor de 14 dimensões. Cada valor é normalizado para 0.0-1.0, exceto os valores sentinela que são -1.

| #  | Dimensão               | Origem                                           | Fórmula                                     | 0.0 significa       | 1.0 significa        |
|----|------------------------|--------------------------------------------------|---------------------------------------------|----------------------|----------------------|
| 1  | amount                 | `transaction.amount`                             | min(amount / max_amount, 1.0)               | valor baixo          | valor alto           |
| 2  | installments           | `transaction.installments`                       | min(installments / max_installments, 1.0)   | poucas parcelas      | muitas parcelas      |
| 3  | amount_vs_avg          | `transaction.amount / customer.avg_amount`       | min(ratio / amount_vs_avg_ratio, 1.0)       | gasto normal         | muito acima da média |
| 4  | hour_of_day            | `transaction.requested_at`                       | hour / 23 (0-23, UTC)                       | meia-noite           | fim do dia           |
| 5  | day_of_week            | `transaction.requested_at`                       | day / 6 (seg=0, dom=6)                      | segunda              | domingo              |
| 6  | minutes_since_last_tx  | `requested_at - last_transaction.timestamp`      | min(minutes / max_minutes, 1.0) ou -1       | acabou de acontecer  | faz tempo            |
| 7  | km_from_last_tx        | `last_transaction.km_from_current`               | min(km / max_km, 1.0) ou -1                 | mesmo lugar          | longe                |
| 8  | km_from_home           | `terminal.km_from_home`                          | min(km / max_km, 1.0)                       | em casa              | longe de casa        |
| 9  | tx_count_24h           | `customer.tx_count_24h`                          | min(count / max_tx_count_24h, 1.0)          | poucas transações    | muitas transações    |
| 10 | is_online              | `terminal.is_online`                             | 0.0 = físico, 1.0 = online                 | loja física          | online               |
| 11 | card_present           | `terminal.card_present`                          | 0.0 = não presente, 1.0 = presente         | não presente         | presente             |
| 12 | is_new_merchant        | `merchant.id` não está em `customer.known_merchants` | 0.0 = conhecido, 1.0 = novo            | comerciante conhecido| primeira vez         |
| 13 | mcc_risk               | lookup `merchant.mcc` em `mcc_risk.json`         | valor direto (já é 0..1)                    | categoria segura     | categoria arriscada  |
| 14 | merchant_avg_amount    | `merchant.avg_amount`                            | min(avg / max_merchant_avg_amount, 1.0)     | loja de ticket baixo | loja de ticket alto  |

**Regras sentinela:**
- Dimensões 6 e 7 usam `-1` quando `last_transaction` é `null`
- MCC não encontrado em `mcc_risk.json` usa `0.5` como default


## Algoritmo KNN

```
1. Receber requisição
2. Normalizar os 14 campos em um vetor 0..1
3. Calcular a distância para cada vetor no dataset de referência
4. Pegar os K vizinhos mais próximos
5. Contar labels fraude vs legítimo
6. fraud_score = fraud_count / K
7. approved = fraud_score < threshold
```

**Parâmetros:**
- **K = 5** — número de vizinhos
- **Métrica de distância** — distância por cosseno
- **Threshold = 0.6** — se fraud_score >= 0.6, a transação não é aprovada

**Labels:** apenas dois valores — `"fraud"` e `"legit"`.


## Arquivos de Referência

Três arquivos são fornecidos aos participantes, que devem inclui-los no container.

### `resources/references.json` — vetores de referência rotulados

```json
[
  {
    "vector": [0.08, 0.50, 0.60, 0.52, 0.83, 0.03, 0.07, 0.36, 0.25, 0.0, 1.0, 0.0, 0.15, 0.02],
    "label": "fraud"
  },
  {
    "vector": [0.02, 0.08, 0.10, 0.35, 0.17, 0.52, 0.01, 0.01, 0.15, 0.0, 1.0, 0.0, 0.10, 0.03],
    "label": "legit"
  }
]
```

Cada vetor tem 14 dimensões conforme a tabela acima. Vetores com valores sentinela (`-1`) nas dimensões 6 e 7 estão incluídos para transações sem histórico prévio.

### `resources/mcc_risk.json` — scores de risco por MCC

```json
{
  "5411": 0.15,
  "5812": 0.30,
  "7995": 0.85,
  "5944": 0.45
}
```

Mapeia código MCC para score de risco (0.0 a 1.0). Se o MCC da requisição não estiver neste arquivo, usar `0.5` como default.

### `resources/normalization.json` — constantes de normalização

```json
{
  "max_amount": 10000,
  "max_installments": 12,
  "amount_vs_avg_ratio": 10,
  "max_minutes": 1440,
  "max_km": 1000,
  "max_tx_count_24h": 20,
  "max_merchant_avg_amount": 10000
}
```

Usadas nas fórmulas de normalização da tabela de dimensões.


## Infraestrutura

Setup padrão da Rinha:
- 2 instâncias da API atrás de um nginx (balanceamento round-robin)
- Limites de recursos por instância (CPU e memória) — a definir
- Deploy via `docker-compose`
- Stateless — sem estado compartilhado entre instâncias


## Desenvolvimento

### Pré-requisitos

Usando Nix:
```bash
nix-shell
```

Ou instale manualmente: `gcc`, `make`, `k6`, `jq`.

### Build do gerador de dados

```bash
cd data-generator
make
cd ..
```

### Gerar dados de teste

```bash
# defaults: 200 referências, 1000 payloads, 30% de fraude
./data-generator/generate

# tamanhos customizados
./data-generator/generate --refs 500 --payloads 5000

# taxa de fraude customizada (50% fraude)
./data-generator/generate --fraud-ratio 0.50

# caminhos de saída customizados
./data-generator/generate --refs-out /tmp/refs.json --payloads-out /tmp/data.json

# todas as opções
./data-generator/generate --refs 500 --payloads 5000 --fraud-ratio 0.40 \
    --norm-cfg resources/normalization.json --mcc-cfg resources/mcc_risk.json \
    --refs-out resources/references.json --payloads-out test/test-data.json

# ver todas as opções
./data-generator/generate --help
```

Arquivos gerados:
- `resources/references.json` — vetores rotulados para o modelo de detecção de fraude
- `test/test-data.json` — payloads de teste com respostas esperadas

### Rodar os testes

Os testes de carga usam [k6](https://k6.io/) e enviam as transações definidas em `test/test-data.json`.

```bash
./run.sh
```

Cada entrada no arquivo de teste contém:
- `request`: o payload enviado para a API
- `info.expected_response`: a resposta esperada (`approved` e `fraud_score`)
- `info.vector`: o vetor correspondente


## Submissão

Adicione um arquivo JSON em `participants/` com seu identificador (ex: `participants/seu-nome.json`):

```json
[
    {
        "id": "minha-submissao",
        "repo": "https://github.com/seu-usuario/seu-repo"
    }
]
```

Seu repositório precisa ter um `docker-compose.yml` que exponha a API na porta `9999`.

### Exemplo

- [Clojure](https://github.com/zanfranceschi/rinha-de-backend-2026-exemplo-clojure) — veja [`participants/zanfranceschi.json`](participants/zanfranceschi.json)

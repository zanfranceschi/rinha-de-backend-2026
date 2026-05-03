# Submissão

Este documento descreve como submeter o seu backend para a Rinha de Backend 2026.

## Inscrição

Para que o seu backend seja oficialmente testado, você deve abrir um [pull request](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request) adicionando um arquivo JSON no diretório [./participants](./participants) com informações sobre a sua participação.

O nome do arquivo deve ser exatamente o seu usuário no github.com — por exemplo, `./participants/ana.json`. O seu repositório git precisa ser e se manter público.

Exemplo do conteúdo do arquivo para uma submissão:

```json
[{
    "id": "ana-elixir",
    "repo": "https://github.com/ana/rinha-de-backend-2026-ana-elixir"
}]
```

O arquivo é um array, então você pode submeter mais de um backend no mesmo arquivo:

```json
[{
    "id": "ana-elixir",
    "repo": "https://github.com/ana/rinha-de-backend-2026-ana-elixir"
},
{
    "id": "ana-experimental",
    "repo": "https://github.com/ana/rinha-de-backend-2026-ana-experimental"
},
{
    "id": "ana-custom-vector-db",
    "repo": "https://github.com/ana/rinha-de-backend-2026-custom-vector-db"
}]
```

## Estrutura do repositório

O seu repositório deve ter, no mínimo, duas branches:

- A branch principal — geralmente chamada `main` — com o código-fonte do seu backend.
- Uma branch chamada `submission`, contendo apenas os arquivos necessários para o teste ser executado. O código-fonte não pode estar nessa branch.

Exemplo de estrutura de diretórios por branch:

```
# branch main
├── src/
│   ├── index.js
│   ├── routes.js
│   └── vectorSearch.js
├── info.json
├── package.json
├── package-lock.json
└── README.md

# branch submission
├── docker-compose.yml
├── nginx.conf
├── info.json
└── init.sql
```

O arquivo `docker-compose.yml` deve estar na raiz da branch `submission`. Sem ele, não é possível executar o teste do seu backend.

### Arquivo `info.json`

Você também deve adicionar um arquivo `info.json` com os campos abaixo. Ele nos ajuda a analisar quais tecnologias foram mais usadas nessa edição e a encontrar você caso precisemos — por exemplo, para anunciar uma vitória ou passar uma indicação adiante.

```json
{
    "participants": ["José Alves", "Ana Zanfranceschi"],
    "social": ["https://github.com/ja/", "https://www.linkedin.com/in/anazan"],
    "source-code-repo": "https://github.com/100f/rinha-backend-2025",
    "stack": ["java", "postgres", "nginx", "redis"],
    "open_to_work": true
}
```

Sobre o campo **open_to_work**: se você estiver em busca de novas oportunidades, defina como `true` para que as pessoas saibam.

## Prévia e teste final

Existem dois momentos de teste na Rinha:

- **Testes de prévia** — você pode submeter seu backend a quantos testes de prévia quiser – eles são como um simulado para os testes finais. Basta abrir uma issue [como essa](https://github.com/zanfranceschi/rinha-de-backend-2026/issues/49) colocando `rinha/test [id opcional da sua submissão]` na descrição da issue. A Engine da Rinha varre todas as issues abertas com essa descrição, executa um teste de prévia, posta os resultados (ou erro) junto com sua pontuação em formato de comentário e fecha a issue. Use e abuse das prévias para fazer pequenos ajustes, testar diferentes configurações, etc.

- **Teste final** — roda uma única vez, ao final da Rinha, e é o que define a pontuação oficial de cada participante. Ele usa um script diferente do script de prévia — provavelmente mais pesado, capaz de exigir mais do seu backend (mais volume, mais carga, cenários diferentes). A data do teste final ainda não está definida.

### Ambiente de testes

A Rinha desse ano roda em um [Mac Mini Late 2014](https://support.apple.com/en-us/111931) com Ubuntu 24.04.

Especificações:

- 2.6 GHz
- 8 GB de RAM
- 1 TB de storage

A casa Rinha dessa edição!
![mac mini da rinha](/misc/macmini-rinha.png)
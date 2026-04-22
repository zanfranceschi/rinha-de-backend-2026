# Submissão

Como participar da Rinha de Backend 2026.

## Inscrição

Para participar e ter seu backend oficialmente testado, você precisa abrir um [pull request](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request) adicionando um arquivo contendo informações sobre sua participação. Por exemplo:

```json
[{
    "id": "ana-elixir",
    "repo": "https://github.com/ana/rinha-de-backend-2026-ana-elixir"
}]
```

Note que esse arquivo contém um array para que você possa submeter mais de um backend. Por exemplo:
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

O nome do arquivo deve ser exatamente como o nome do seu usuário no github.com e dentro do diretório [./participants](./participants). Por exemplo: `./participants/ana.json`. Obviamente, seu repositório git deve ser público!

## Estrutura do repositório

Seu repositório **PRECISA** conter no mímino duas branches:
- A branch principal – geralmente chamada `main` – contendo o código fonte do seu backend.
- Uma branch chamada `submission`. A branch submission deve possuir apenas os arquivos para que o teste seja executado (não deve conter o código fonte)!

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

Note que é essencial haver o arquivo `docker-compose.yml` na raiz da branch `submission`. Caso contrário, não será possível executar o teste do seu backend.

Por favor, também adicione um arquivo `info.json` com os seguintes campos para que consigamos analisar as tecnologias mais usadas nessa edição da rinha e encontrar você caso necessário para anunciar uma vitória, fazer uma indicação, etc.

```json
{
    "participants": ["José Alves", "Ana Zanfranceschi"],
    "social": ["https://github.com/ja/", "https://www.linkedin.com/in/anazan"],
    "source-code-repo": "https://github.com/100f/rinha-backend-2025",
    "stack": ["java", "postgres", "nginx", "redis"],
    "open_to_work": true    
}
```
Sobre o campo **open_to_work**: A Rinha já ajudou muita gente profissionalmente! Então, para que as pessoas saibam que você está em busca de novas oportunidades, defina esse campo para `true`.


## Execução do teste

Para que seu backend de fato passe pelo teste, é necessário abrir uma [issue](https://github.com/zanfranceschi/rinha-de-backend-2026/issues) colocando `rinha/test` na descrição. Se você tiver mais de uma submissão, informe também o `id` – por exemplo, `rinha/test ana-experimental`.

Essa edição da Rinha de Backend conta com uma engine que varre as issues abertas com essa descrição, executa os testes, posta o resultado dos testes (ou erro) em forma de comentário e fecha a issue. Para submeter seu backend a um novo teste, basta abrir uma nova issue.

![alt text](open-issue.png)


#### Especificações do ambiente de testes

Essa ano a Rinha está usando um [Mac Mini Late 2014 Edition](https://support.apple.com/en-us/111931) com Ubuntu 24.04 instalado.

Versão do Mac Mini:
- 2.6GHz
- 8GB de RAM
- 1TB de Storage

## Data limite

A data limite para submissão ainda não está definida.

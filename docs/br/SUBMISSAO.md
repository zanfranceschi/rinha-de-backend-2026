# Submissão

Como enviar sua submissão para a Rinha de Backend 2026.

## Pré-requisitos

> TODO: checklist antes de submeter.

- [ ] API responde `GET /ready` com 200 quando pronta
- [ ] API responde `POST /fraud-score` conforme especificação
- [ ] Container roda com os limites de CPU/memória definidos em ARQUITETURA.md
- [ ] `docker-compose.yml` segue o template
- [ ] Dataset (`references.json.gz`) incluído no container

## Estrutura do Repositório

> TODO: descrever layout esperado em `participants/SEU_NOME/`.

```
participants/
  SEU_NOME/
    docker-compose.yml
    README.md           # sua tecnologia, abordagem, repositório fonte
    # ... outros arquivos necessários
```

## Passo-a-passo

> TODO: descrever fluxo (fork, branch, PR, review, merge).

1. Fork do repositório
2. Crie sua pasta em `participants/SEU_NOME/`
3. Adicione seu `docker-compose.yml` e README
4. Abra um Pull Request com o título `[submissão] SEU_NOME`

## Data limite

> TODO: definir.

## Checklist final

> TODO: lista de verificação pré-PR.

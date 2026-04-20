# Arquitetura e Restrições

Setup padrão da Rinha adaptado para esta edição.

## Topologia

> TODO: diagrama mermaid mostrando nginx + 2 instâncias da API.

- 2 instâncias da API atrás de um nginx (balanceamento round-robin)
- Deploy via `docker-compose`
- Stateless — sem estado compartilhado entre instâncias
- Sem banco de dados — dataset carregado em memória na inicialização

## Limites de Recursos

> TODO: definir e detalhar.

- CPU total: *a definir*
- Memória total: *a definir*
- Limites por serviço (nginx, API × 2): *a definir*

## Portas

> TODO: porta de exposição (9999?), porta interna, nginx upstream.

## Formato do `docker-compose.yml`

> TODO: template + restrições (nomes de serviços, healthcheck, depends_on, etc).

## `GET /ready`

Health check. Deve retornar `200` quando a API estiver pronta para receber requisições (dataset carregado, dependências inicializadas).

## Regras gerais

> TODO: listar (imagens permitidas, linguagens, uso de bancos vetoriais em container separado, etc).

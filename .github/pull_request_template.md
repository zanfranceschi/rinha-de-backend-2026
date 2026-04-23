<!--
🇧🇷 Use este PR apenas para adicionar `./participants/<seu-usuario>.json`.
🇺🇸 Use this PR only to add `./participants/<your-username>.json`.

🇧🇷 Se este PR for de docs/engine/infra, remova o checklist abaixo.
🇺🇸 If this PR is for docs/engine/infra, remove the checklist below.
-->

## Descrição / Description

<!--
🇧🇷 Explique resumidamente o que este PR faz.
🇺🇸 Briefly explain what this PR does.
-->

## Checklist da submissão / Submission checklist

<!--
🇧🇷 Marque apenas os itens que se aplicam a este PR.
🇺🇸 Check only the items that apply to this PR.
-->

- [ ] 🇧🇷 O total entre os serviços respeita o limite de **1 CPU** e **350MB RAM**<br>🇺🇸 Total across services respects the limit of **1 CPU** and **350MB RAM**
- [ ] 🇧🇷 Backend expõe a porta **9999**<br>🇺🇸 Backend exposes port **9999**
- [ ] 🇧🇷 Imagens são **linux/amd64**<br>🇺🇸 Images are **linux/amd64**
- [ ] 🇧🇷 Rede em modo **bridge**<br>🇺🇸 Network mode is **bridge**
- [ ] 🇧🇷 Não usa `network_mode: host` nem `privileged`<br>🇺🇸 Does not use `network_mode: host` nor `privileged`
- [ ] 🇧🇷 Possui **≥ 1 load balancer + 2 APIs**, e o LB não contém lógica de aplicação (lei Gabriel-2025)<br>🇺🇸 Has **≥ 1 load balancer + 2 APIs**, and the LB contains no application logic (Gabriel-2025 law)
- [ ] 🇧🇷 Repositório é **público** e contém as branches `main` e `submission`<br>🇺🇸 Repository is **public** and contains branches `main` and `submission`
- [ ] 🇧🇷 Branch `submission` contém na raiz `docker-compose.yml` e `info.json`<br>🇺🇸 Branch `submission` contains `docker-compose.yml` and `info.json` at repo root

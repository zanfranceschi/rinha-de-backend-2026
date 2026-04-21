# Submission

How to participate in Rinha de Backend 2026.

## Registration

To participate and have your backend officially tested, you need to open a [pull request](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request) adding a file containing your participation info. For example:

```json
[{
    "id": "ana-elixir",
    "repo": "https://github.com/ana/rinha-de-backend-2026-ana-elixir"
}]
```

Note that this file contains an array so you can submit more than one backend. For example:
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

The filename must be exactly your github.com username and placed inside the [./participants](./participants) directory. For example: `./participants/ana.json`. Obviously, your git repository must be public!

## Repository structure

Your repository **MUST** contain at least two branches:
- The main branch – usually called `main` – containing your backend source code.
- A branch called `submission`. The submission branch must contain only the files required to execute the test (it must not contain the source code)!

Example directory structure per branch:
```
# main branch
├── src/
│   ├── index.js
│   ├── routes.js
│   └── vectorSearch.js
├── info.json
├── package.json
├── package-lock.json
└── README.md

# submission branch
├── docker-compose.yml
├── nginx.conf
└── init.sql
```

## Test Execution

For your backend to actually go through the test, you must open an [issue](https://github.com/zanfranceschi/rinha-de-backend-2026/issues) with `rinha/test` in the description. If you have more than one submission, also provide the `id` – for example, `rinha/test ana-experimental`.

This Rinha de Backend edition has an engine that scans open issues with this description, runs the tests, posts the test result (or error) as a comment, and closes the issue. To resubmit your backend for a new test, just reopen the issue or open a new one. It's recommended to reopen the same issue so that comparisons with previous versions of your backend are easier to make.

![alt text](../br/open-issue.png)


## Deadline

The submission deadline has not been defined yet.

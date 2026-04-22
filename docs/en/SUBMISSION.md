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
├── info.json
└── init.sql
```

Note that having the `docker-compose.yml` file at the root of the `submission` branch is essential. Otherwise, it won't be possible to run the test for your backend.

Please also add an `info.json` file with the following fields so we can analyze the most used technologies in this edition of Rinha and reach you if needed to announce a win, make a referral, etc.

```json
{
    "participants": ["José Alves", "Ana Zanfranceschi"],
    "social": ["https://github.com/ja/", "https://www.linkedin.com/in/anazan"],
    "source-code-repo": "https://github.com/100f/rinha-backend-2025",
    "stack": ["java", "postgres", "nginx", "redis"],
    "open_to_work": true    
}
```
About the **open_to_work** field: Rinha has already helped many people professionally! So, to let people know you're looking for new opportunities, set this field to `true`.


## Test Execution

For your backend to actually go through the test, you must open an [issue](https://github.com/zanfranceschi/rinha-de-backend-2026/issues) with `rinha/test` in the description. If you have more than one submission, also provide the `id` – for example, `rinha/test ana-experimental`.

This Rinha de Backend edition has an engine that scans open issues with this description, runs the tests, posts the test result (or error) as a comment, and closes the issue. To resubmit your backend for a new test, just open a new issue.

![alt text](../br/open-issue.png)


#### Test environment specifications

This year Rinha is using a [Mac Mini Late 2014 Edition](https://support.apple.com/en-us/111931) running Ubuntu 24.04.

Mac Mini specs:
- 2.6GHz
- 8GB of RAM
- 1TB of Storage

## Deadline

The submission deadline has not been defined yet.

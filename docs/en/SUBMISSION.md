# Submission

This page describes how you can take part in Rinha de Backend 2026.

## Important!

- To participate in Rinha, all your repositories must be under the MIT license.
- Using the test payloads as a lookup is not allowed.
- If you disrupt the event, you will be removed without prior notice.

## Registration

To register your backend for official testing, you need to open a [pull request](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request) that adds a file with your participation details. An example:

```json
[{
    "id": "ana-elixir",
    "repo": "https://github.com/ana/rinha-de-backend-2026-ana-elixir"
}]
```

The file holds an array, so you can submit more than one backend if you want. For example:

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

The filename must be exactly your github.com username, placed inside the [./participants](./participants) directory — for example, `./participants/ana.json`. Your git repository also needs to be and remain public.

## Repository structure

Your repository must have at least two branches:

- The main branch — usually called `main` — which holds your backend source code.
- A branch called `submission`. This branch must contain only the files needed to run the test, and it cannot contain the source code.

Here is an example of the directory structure on each branch:

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

The `docker-compose.yml` file must sit at the root of the `submission` branch. Without it, your backend cannot be tested.

Your repository also needs an `info.json` file with the fields below. This helps us understand which technologies are most used in this edition of Rinha, and lets us reach you if we need to — to announce a win, make a referral, and so on.

```json
{
    "participants": ["José Alves", "Ana Zanfranceschi"],
    "social": ["https://github.com/ja/", "https://www.linkedin.com/in/anazan"],
    "source-code-repo": "https://github.com/100f/rinha-backend-2025",
    "stack": ["java", "postgres", "nginx", "redis"],
    "open_to_work": true
}
```

A note on the **open_to_work** field: if you are looking for new opportunities and want people to know about it, set this field to `true`.

## Preview and final test

There are two testing moments in Rinha:

- **Preview tests** — you can submit your backend to as many preview tests as you want – they serve as a practice run for the final test. Just open an issue [like this one](https://github.com/zanfranceschi/rinha-de-backend-2026/issues/49) with `rinha/test [optional id of your submission]` in the issue description. The Rinha Engine scans all open issues with that description, runs a preview test, posts the results (or any error) together with your score as a comment, and closes the issue. Take full advantage of preview tests to make small adjustments, try different configurations, and so on.

- **Final test** — runs a single time, at the end of Rinha, and it is what defines the official score of each participant. It uses a different script from the preview script — likely heavier, capable of demanding more from your backend (more volume, more load, different scenarios). The date of the final test is not defined yet.

### Test environment

This year, Rinha runs on a [Mac Mini Late 2014](https://support.apple.com/en-us/111931) with Ubuntu 24.04.

Specs:

- 2.6 GHz
- 8 GB of RAM
- 1 TB of storage

Home of this edition's Rinha!
![rinha's mac mini](/misc/macmini-rinha.png)


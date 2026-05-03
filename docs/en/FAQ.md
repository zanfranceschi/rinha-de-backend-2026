# FAQ

## What you can do

- Use any language, framework, database, or technology you prefer.
- Include vector databases in your compose (pgvector, Qdrant, SQLite-vss, and so on).
- Pre-process the dataset during the container build.
- Use exact KNN, approximate ANN, or any other classifier.
- Suggest improvements or report issues for this edition. Contributions are welcome — just open a pull request. This is a one-person project, so mistakes are expected.
- Run the challenge at home without submitting, if that is what works for you.

## What you cannot do

- Use a load balancer that performs any fraud-detection logic.
- Try to hide your submission or part of it (source code) to prevent others from learning from it.
- Disrespect other participants or the people organizing the event.
- Make demands of organizers or participants. Nobody is profiting from this; the goal is to learn and have fun.
- Use the test payloads as a reference or for fraud lookup!

## Common mistakes

- Leaving the Docker image referenced in your `docker-compose.yml` private.
- On Mac, building Docker images that only support arm64.
- Keeping your Git repository private.

# Thunderobot AGENTS.md

This file defines the commit and collaboration rules for this repository.

## Atomic Commit

- Each commit must represent one logical change only.
- Do not mix refactors, docs, features, and fixes in one commit.
- Keep commits small, reviewable, and revert-safe.

## Branching

- Work on feature branches.
- Keep the branch name aligned with the change scope.
- Rebase or squash before merge only when it preserves atomic history.

## Validation

- Run the minimal validation needed for the changed area.
- Do not commit known broken code.

## Code Style

- Prefer small focused patches.
- Match existing project style and module boundaries.
- Keep kernel, CLI, and docs changes separated across commits.

# Contributing to TorrentUtils

## Branches

- Create a short-lived branch from the latest `main`.
- Use `feature/<issue>-<slug>`, `fix/<issue>-<slug>`, `docs/<issue>-<slug>`, `chore/<issue>-<slug>`, or `hotfix/<issue>-<slug>`.
- Keep one issue per branch and delete the branch after it is merged.

## Commits

- Use Conventional Commits and identify the affected module in the scope.
- Keep commits small and reviewable; do not mix feature changes with reformatting or unrelated restructuring.

## Pull Requests

- Submit all changes through a pull request into the protected `main`; do not push directly.
- Rebase onto the latest `main` before merging, and ensure all required GitHub Actions checks pass.
- A maintainer should merge the pull request after approval from at least one non-author reviewer.

## Releases

- Create an annotated `vX.Y.Z` tag directly from a merged `main` commit for stable releases.
- Publish only after the tag workflow completes the build, install-consumer checks, SBOM generation, and checksum validation.

## Emergency Fixes

- Create a `hotfix/` branch from the latest `main` and keep one issue per fix.
- The formatting, Linux Core, affected-test, and Windows build gates must pass, along with maintainer approval; create the patch tag after the fix is merged.

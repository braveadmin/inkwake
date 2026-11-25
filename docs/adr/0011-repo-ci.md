# 0011. Repository and CI

- Status: Accepted
- Date: 2026-11-02

## Decision

GitHub monorepo, trunk-based, Conventional Commits, release-please, GitHub Actions, ESP Web Tools flasher on Pages.

## Alternatives rejected

Polyrepo.

## Rationale

One tag produces matching firmware, STLs and docs.

## Amendment (2026-11-25)

Releases attach firmware only until the case is measured and test-printed (S1/S3): the enclosure
dimensions are still placeholders, and an STL in a release reads as ready to print. CI keeps
building the STLs as artifacts (enclosure.yml); they return to releases with S3.

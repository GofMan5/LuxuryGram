# Security Policy

## Supported versions

LuxuryGram is currently a source preview with no official binary release.

| Version | Security support |
| --- | --- |
| Current `dev` branch | Best-effort source fixes |
| Unofficial binaries or downstream packages | Not supported by this project |
| AyuGram or Telegram Desktop releases | Report to their respective projects |

This table will be replaced with explicit release ranges after the first stable LuxuryGram release.

## Report a vulnerability privately

Use [GitHub private vulnerability reporting](https://github.com/GofMan5/LuxuryGram/security/advisories/new). Do not open a public issue for an undisclosed vulnerability.

Include, when available:

- affected commit, platform, and configuration;
- a concise impact statement;
- exact reproduction steps or a minimal proof of concept;
- logs with credentials, phone numbers, session material, and private messages removed;
- suggested remediation or disclosure constraints.

Use a disposable test account and synthetic data. Do not access other people's accounts, messages, devices, or infrastructure while researching a report.

## What happens next

Maintainers will triage the report, reproduce it where practical, determine whether it belongs to LuxuryGram or an upstream dependency, and coordinate a fix and disclosure. Response timing is best effort while the project is in source-preview status.

If the issue is entirely in Telegram Desktop, AyuGram, Qt, or another dependency, the reporter may be asked to coordinate with that upstream project.

## Supply-chain guidance

Until official releases are published, build from this repository and verify the commit you checked out. After releases begin, trust only artifacts attached to this repository's [Releases](https://github.com/GofMan5/LuxuryGram/releases) page and the verification information published with that release.

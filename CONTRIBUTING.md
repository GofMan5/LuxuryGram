# Contributing to LuxuryGram

Thank you for improving LuxuryGram. Small, focused, tested contributions are the easiest to review and maintain.

## Before you start

- Search [open issues](https://github.com/GofMan5/LuxuryGram/issues) before creating a duplicate.
- Use a bug report for reproducible defects and a feature request for new behavior.
- Report vulnerabilities privately through the process in [`SECURITY.md`](SECURITY.md).
- Keep account data, API credentials, phone numbers, session files, and private messages out of issues and pull requests.

## Set up the repository

Fork the project on GitHub, then clone your fork with submodules:

```bash
git clone --recursive https://github.com/YOUR_ACCOUNT/LuxuryGram.git
cd LuxuryGram
git remote add upstream https://github.com/GofMan5/LuxuryGram.git
git fetch upstream dev
git switch -c fix/short-description upstream/dev
```

If submodules are missing:

```bash
git submodule update --init --recursive
```

Build instructions:

- [Windows](docs/building-win.md)
- [macOS](docs/building-mac.md)
- [Linux](docs/building-linux.md)
- [Telegram API credentials](docs/api_credentials.md)

## Make a good change

1. Trace the relevant flow and reuse an existing project pattern when possible.
2. Fix the shared root cause rather than one visible symptom.
3. Keep one pull request focused on one problem.
4. Do not mix functional edits with unrelated formatting, generated output, or submodule updates.
5. Preserve compatibility-critical Telegram and AyuGram identifiers unless the change includes a tested migration.
6. Put user-visible text in the localization system and scalable UI dimensions in `.style` files.
7. Follow [`REVIEW.md`](REVIEW.md) for C++/Qt conventions.

## Verify it

Run the smallest check that proves the changed behavior, plus the affected Debug build when a configured build tree is available. In the pull request, list:

- the exact commands you ran;
- their results;
- the platforms you tested;
- anything that remains unverified.

Visual changes should include clear before/after screenshots. Never include real chats, phone numbers, tokens, or other personal data.

## Commit and open a pull request

Use a short, imperative commit subject that describes the result. Avoid tool attribution and unrelated history rewrites.

Before opening the pull request:

```bash
git diff --check
git status --short
```

Open the pull request against `GofMan5/LuxuryGram:dev`, complete the template, and link the relevant issue with `Fixes #123` when appropriate. Maintainers may ask for a rebase or a smaller split, but contributors are not required to squash everything into one commit before review.

## Licensing

By submitting a contribution, you agree that it may be distributed under the repository's [`LICENSE`](LICENSE). You retain copyright in your contribution.

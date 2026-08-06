# LuxuryGram Agent Guide

## Product and repository

- This repository builds **LuxuryGram** from the AyuGram source line while remaining a GitHub fork of `telegramdesktop/tdesktop`.
- Use **LuxuryGram** for user-facing product identity. Do not blindly rename protocol, API, schema, namespace, build-target, storage, or upstream identifiers when compatibility requires `Telegram`, `tdesktop`, `tg`, or an upstream project name.
- Preserve license notices, copyright, third-party attribution, and upstream links unless a task explicitly replaces a project-owned link.
- Treat `origin` as the LuxuryGram repository, `ayugram` as the source-line reference, and `upstream` as Telegram Desktop. Verify current remotes before network or history operations.

## Working rules

1. Read the request, `git status`, relevant files, callers, and existing patterns before editing.
2. Preserve unrelated work. Never reset, clean, stash, stage all files, or rewrite history to make the tree look clean.
3. Fix the shared root cause once instead of adding path-specific guards to every caller.
4. Reuse repository code, platform facilities, standard library, and installed dependencies in that order. Add no speculative abstraction or dependency.
5. Keep the diff focused, but do not simplify away validation, error handling, security, accessibility, compatibility, or requested behavior.
6. Infer ordinary implementation details from repository evidence. Ask only when a destructive or externally visible choice cannot be recovered or safely inferred.
7. Do not commit, push, publish, create a release, or open a pull request unless the current task requests delivery. Stage only explicit task-owned paths.

## Branding

A full rebrand must account for every user-visible surface that exists in the repository, including:

- application names, titles, About text, update text, links, and localization sources;
- Windows resources and version metadata, executable/file descriptions, installer/package metadata, protocol registration, shortcuts, notifications, and portable-mode names;
- macOS and Linux bundle/desktop metadata when those targets exist;
- application icons, tray/taskbar icons, notification icons, installer art, and documentation images;
- repository README, build/package scripts, CI/release metadata, and project-owned URLs.

Before declaring a rebrand complete, inventory remaining legacy names with reproducible searches. Classify each match as user-facing and changed, compatibility-critical and retained, third-party/upstream attribution and retained, or generated/vendor content and excluded. Do not report raw search counts as completion evidence without that classification.

For supplied artwork, preserve the original, derive assets from the highest-resolution source, and verify dimensions, alpha behavior, edge padding, and representative rendered sizes. Do not stretch one raster into every platform format.

## Build and verification

- Missing `out/`, a Debug executable, or an authenticated portable test account is **not** a global implementation blocker. Continue with source, asset, metadata, and focused checks; report only the runtime checks that remain unavailable.
- Detect the real host and configured build tree. Do not assume WSL, a drive letter, dependency path, executable suffix, or prior build configuration.
- Use Debug builds only unless the user explicitly requests Release. For an existing native build tree, the usual command is `cmake --build out --config Debug --target Telegram`.
- Do not configure or download a large toolchain merely to satisfy a cosmetic check when a smaller repository-provided validator proves the change.
- Run the smallest check that can fail for the changed behavior. Non-trivial logic needs one focused runnable regression check.
- Runtime tests that need an account may use only a clearly disposable prepared profile. Never move, overwrite, copy, or delete a real profile as an implicit test setup.
- If a build reports a locked PDB/output, `C1041`, `LNK1104`, access denied, or file-in-use error, stop that build after the first failure and report the exact locked path. Do not delete or work around locked outputs.
- Never claim a build, launch, visual result, or platform package was verified unless its command and result were observed in this task.

## Docker and BuildKit

After any task that used Docker or BuildKit:

1. Run `docker builder prune --all --force` at the end.
2. Run `docker system df` and check free space on the relevant disks.
3. Delete only build cache and temporary build artifacts created by the task.
4. Preserve containers, images, volumes, projects, and recovery/VHDX files unless the user explicitly requests their deletion.

## Code and data conventions

- Follow `REVIEW.md` for C++/Qt formatting and review rules.
- Prefer self-documenting code; do not add comments that merely narrate the next line.
- Use scalable style metrics from `.style` files for UI geometry instead of raw pixel constants in C++.
- Put user-visible strings in the localization system instead of scattering literals through code.
- Sequential `QDataStream` settings fields are append-only. Guard new reads with `!stream.atEnd()` and preserve meaningful defaults; prefer existing key-value preferences for simple new values.
- For the project-wide Unix fallback, prefer `!defined Q_OS_WIN && !defined Q_OS_MAC`; use `Q_OS_LINUX` only for genuinely Linux-specific behavior.
- Preserve each edited file's established line endings and encoding. Use UTF-8 without BOM for rewritten project text; never normalize unrelated files.

## Completion

- Reopen every changed artifact and review the final diff.
- Report exact checks and their outcomes, remaining unverified surfaces, and all changed paths.
- Leave unrelated files untouched and the task-owned diff reproducible and reversible.
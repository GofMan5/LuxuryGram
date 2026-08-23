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

## Releases

A release publishes one verified LuxuryGram product version on GitHub Releases. Tags use `luxury-v<product version>`, for example `luxury-v1.0.0`. The clone also carries the inherited Telegram Desktop tags `v0.x`-`v7.x`; never reuse, move, or push one of those to `origin`.

### Preconditions

1. `dev` is clean, equal to `origin/dev`, and `0 behind` upstream.
2. Linux and Windows CI are green for the exact release commit. Record both run IDs.
3. `LuxuryVersionStr` equals the version being released, and `VERSIONING.md`, `CHANGELOG.md`, and README version references agree. Never bump the version to enable a release; a bump needs an explicit request.
4. `CHANGELOG.md` has a `## <version>` section for the release, and no remaining wording that the release makes false.
5. The tag does not exist locally or on `origin`.

### Procedure

1. Commit the release documentation, then push `dev`.
2. `git tag -a luxury-v<version> <sha> -m "LuxuryGram <version>"` and `git push origin luxury-v<version>`.
3. `gh workflow run release.yml --ref dev -f tag=luxury-v<version>`. Dispatch from `dev`, not from the tag: the workflow definition and the Actions caches both come from the default branch, while every job checks out the tag. `release.yml` builds Release-configuration Windows x64 and Linux x64 portable archives, writes `SHA256SUMS.txt`, and creates or updates a **draft** release.
4. When all jobs finish, download every asset, verify each SHA-256 against `SHA256SUMS.txt`, and inspect the archive layout and the packaged executable metadata.
5. Publish only after that check: `gh release edit luxury-v<version> --draft=false --latest`.
6. Point the updater at the new build, which is a deliberate second step so no client updates to an unverified release:

   ```
   gh release download luxury-v<version> -p current4 -D .
   gh release upload updates current4 --clobber
   ```

7. Record run IDs, asset names with hashes, and the release URL in the task delivery record under `.ai/delivery/`.

### Updates

- Clients poll `https://github.com/GofMan5/LuxuryGram/releases/download/updates/current4` and download the package it names from the same release. The `updates` release is a container, never a downloadable build: keep it out of `--latest`.
- Packages are signed by `Packer` with RSA-4096 over SHA-256 and verified against `UpdatesPublicKey` in `Telegram/SourceFiles/config.h` before anything is unpacked. Never publish a package built without the key, and never weaken the verification to make a build pass.
- The private key lives in the `LUXURY_UPDATE_PRIVATE_KEY` repository secret and is written to `Telegram/SourceFiles/_other/packer_private.h` at build time by `Telegram/build/write_packer_private.py`. That header is gitignored and must never be committed. Losing the key means shipping a new public key and asking every user to reinstall by hand.
- Update packages are compared by `LuxuryUpdateVersion` (`major * 1000000 + minor * 1000 + patch`), not by the upstream `AppVersion`. A version bump must move both, or `release.yml` refuses to build.
- `release.yml` uploads the signed packages before the feed names them, so the assets always exist by the time a client is told about them.
- `Packer` is built next to the application and carries the private key compiled into it. Packaging names the files it ships; never copy a build directory by extension, and never attach a build directory to a release or a workflow artifact. Artifacts of a public repository are downloadable by anyone.
- Releases need `default_workflow_permissions` set to `write` for this repository, or the workflow fails with `403 Resource not accessible by integration`. Changing that setting does not reach a run that already exists: re-running a job keeps the permissions its run was created with, so dispatch a new run instead of re-running the old one.

### Artifact rules

- Only Release-configuration builds may be published. The `win.yml` and `linux.yml` Debug artifacts are gating evidence, never release assets.
- Ship portable archives with the `Updater` binary alongside the application, so the built-in updater can replace them in place.
- Binaries are unsigned. State that in the notes instead of omitting it.
- `release.yml` uses the `TDESKTOP_API_ID` and `TDESKTOP_API_HASH` repository secrets when they exist and otherwise falls back to the public open credentials. Never commit credentials.

### Rollback

- A draft or bad release: `gh release delete luxury-v<version> --yes`, then `git push origin :refs/tags/luxury-v<version>` and `git tag -d luxury-v<version>`.
- Deleting a published release does not recall downloads. Supersede it with a new patch version instead of moving an existing release tag.

### Known ceilings

- Windows release libraries are prepared without `skip-release`, so a cold run is long. If the job approaches the six-hour limit, split library preparation and the application build into two runs using the `ONLY_CACHE` pattern from `win.yml`.
- `release.yml` does not build macOS, Windows arm64 or x86, or any installer yet.
- Windows release binaries are linked without debug information: with it, `link.exe` exhausts the runner's memory (`LNK1102`). No symbol server consumes the PDBs, so nothing is lost until one exists.

## Code and data conventions

- Follow `REVIEW.md` for C++/Qt formatting and review rules.
- Prefer self-documenting code; do not add comments that merely narrate the next line. Do not delete an existing comment to satisfy that rule — move it with the code it explains, and drop it only once your change makes it wrong.
- Never discard a result with a cast. `static_cast<void>(...)` and `(void)expr` silence `[[nodiscard]]` instead of answering it; fix the design.
- Use scalable style metrics from `.style` files for UI geometry instead of raw pixel constants in C++.
- Put user-visible strings in the localization system instead of scattering literals through code.
- Sequential `QDataStream` settings fields are append-only. Guard new reads with `!stream.atEnd()` and preserve meaningful defaults; prefer existing key-value preferences for simple new values.
- For the project-wide Unix fallback, prefer `!defined Q_OS_WIN && !defined Q_OS_MAC`; use `Q_OS_LINUX` only for genuinely Linux-specific behavior.
- Preserve each edited file's established line endings and encoding. Use UTF-8 without BOM for rewritten project text; never normalize unrelated files.

## Changelog

`CHANGELOG.md` is the record of what this fork did, for users and for the next agent. Keep it complete enough that nobody has to read the Git history to learn what changed.

- Every change a user could notice gets an entry in the same task that makes the change, not later: new features, behaviour changes, fixes, removals, performance work with a number, and anything that alters what is installed or sent over the network.
- Write entries for the person running the app: what changed and what it means for them. Name the surface (`saved music covers`, `custom forwarding`), not the function.
- Entries land under `## Unreleased` until a version is tagged. Never add to a released section: a published tag cannot gain entries, and claiming work it does not contain is a lie in the release notes the workflow generates from that section.
- When a version is cut, `## Unreleased` becomes `## <version>` and a fresh `## Unreleased` is not created until the next change needs it.
- Group entries under `### Added`, `### Changed`, `### Fixed`, `### Removed`, and `### Security`. Security-relevant changes are always called out, including key rotations and anything that stops data leaving the machine.
- Purely internal work that no user can observe — refactors, CI plumbing, contract checks — stays out of `CHANGELOG.md` and lives in the commit message instead. Deleting a feature is never internal.
- Quantify when a number exists: `169x faster bulk deletion`, `download 93 MB to 65 MB`. A claim without a measurement does not belong in the changelog.

## Completion

- Reopen every changed artifact and review the final diff.
- Confirm every user-visible change of the task has a `CHANGELOG.md` entry under `## Unreleased`.
- Report exact checks and their outcomes, remaining unverified surfaces, and all changed paths.
- Leave unrelated files untouched and the task-owned diff reproducible and reversible.
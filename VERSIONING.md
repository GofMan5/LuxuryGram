# LuxuryGram versioning

The current LuxuryGram product version is **1.0.2**.

## Policy

- Product versions advance only for an explicitly requested bump or a substantial LuxuryGram update.
- Patch releases follow `1.0.0`, `1.0.1`, and so on through `1.0.1000`.
- The `1.1.0` series starts only after explicit maintainer approval.
- Upstream Telegram Desktop synchronization, CI runs, and commit counts never bump the LuxuryGram version automatically.

`LuxuryVersionStr` and the `LuxuryVersionStr*` build fields are the product-version source of truth. The legacy `AppVersionOriginal` build field mirrors that value only because the upstream CMake parser consumes it. `AppVersion` and `AppVersionStr` intentionally remain on the synchronized Telegram Desktop line because local-storage migrations, update compatibility, and protocol diagnostics depend on them.

Every approved bump must update the product constant, build metadata, Windows/UWP resources, changelog, and README together, followed by Linux and Windows verification for the exact release commit.

## Правила на русском

- Текущая версия продукта — **1.0.2**.
- Бамп выполняется только по прямому запросу или для крупного обновления LuxuryGram.
- Патч-линейка идёт от `1.0.0` до `1.0.1000`; переход на `1.1.0` — только после отдельного подтверждения.
- Синхронизация с Telegram Desktop, CI и количество коммитов версию автоматически не меняют.
- Внутренняя upstream-версия сохраняется отдельно ради совместимости данных, обновлений и протокольной диагностики.

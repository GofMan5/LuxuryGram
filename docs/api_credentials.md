# Telegram API credentials

Self-built LuxuryGram clients require a Telegram `api_id` and `api_hash`.

## Create credentials

1. Sign in at <https://my.telegram.org/>.
2. Open **API development tools**.
3. Create an application for your local build.
4. Pass the generated values to CMake as `TDESKTOP_API_ID` and `TDESKTOP_API_HASH`.

Example:

```text
-D TDESKTOP_API_ID=YOUR_API_ID
-D TDESKTOP_API_HASH=YOUR_API_HASH
```

## Keep them private

- Do not copy credentials from official clients or third-party tutorials.
- Do not commit credentials, paste them into issues, or include them in screenshots and logs.
- Store them in a local build preset, environment-specific configuration, or secret manager excluded from Git.
- Follow the [Telegram API Terms of Service](https://core.telegram.org/api/terms) for your application.

If credentials are exposed, rotate them through your Telegram developer account before continuing to use the build.

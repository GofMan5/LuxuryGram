<div align="center">
  <img src=".github/assets/luxurygram-logo.png" width="220" alt="LuxuryGram logo">
  <h1>LuxuryGram</h1>
  <p><strong>A refined, privacy-minded Telegram experience for desktop.</strong></p>
  <p>Open-source · Built from AyuGram · Based on Telegram Desktop</p>

  <p>
    <a href="README.md"><strong>English</strong></a>
    ·
    <a href="README-RU.md">Русский</a>
  </p>

  <p>
    <img alt="License" src="https://img.shields.io/github/license/GofMan5/LuxuryGram?style=for-the-badge&labelColor=0B0E16&color=D8A43A">
    <img alt="C++ and Qt" src="https://img.shields.io/badge/C%2B%2B-Qt_6-D8A43A?style=for-the-badge&logo=cplusplus&logoColor=white&labelColor=0B0E16">
    <img alt="Project status" src="https://img.shields.io/badge/status-source_preview-D8A43A?style=for-the-badge&labelColor=0B0E16">
  </p>

  <p>
    <a href="#highlights">Highlights</a> ·
    <a href="#get-luxurygram">Get LuxuryGram</a> ·
    <a href="#build-from-source">Build</a> ·
    <a href="CONTRIBUTING.md">Contribute</a> ·
    <a href="SECURITY.md">Security</a>
  </p>
</div>

> [!IMPORTANT]
> Official LuxuryGram builds are published only on this repository's [Releases](https://github.com/GofMan5/LuxuryGram/releases) page. They are portable archives that update themselves from this repository, and they are not code-signed, so verify the published `SHA256SUMS.txt` before the first run.

> [!NOTE]
> LuxuryGram is an independent, unofficial client. It is not affiliated with, maintained by, or endorsed by Telegram.

## Highlights

LuxuryGram keeps the familiar Telegram Desktop foundation and adds more control where a desktop client benefits from it.

| Area | What it offers |
| --- | --- |
| **Privacy controls** | Flexible ghost mode and granular presence/activity options. |
| **Message context** | Local edit/delete history and anti-recall tools for supported conversations. |
| **Focus** | Streamer-friendly controls that help hide sensitive information while sharing a screen. |
| **Personalization** | Font controls, appearance options, and a more configurable desktop experience. |
| **Productivity** | Right-click message translation into your selected language, richer media previews, and faster shortcuts. |
| **Desktop foundation** | The mature C++/Qt architecture and platform integration inherited from Telegram Desktop. |

Some features operate locally and cannot change Telegram server-side behavior. Availability may vary by platform and by the current development snapshot.

## Project status

| Surface | Status |
| --- | --- |
| Product version | [`1.0.3`](VERSIONING.md); bumps are manual or tied to substantial updates |
| Source code | Public on the [`dev`](https://github.com/GofMan5/LuxuryGram/tree/dev) branch |
| Windows | Portable `x64` archive published |
| macOS | Build guide available; no published package |
| Linux | Portable `x64` archive published |
| Official package managers | Not published |

The user-facing application identity is LuxuryGram across supported desktop platforms. A small set of inherited AyuGram paths, service endpoints, and attribution references remains intentionally preserved where changing them would break profile, protocol, or source-line compatibility.

## Get LuxuryGram

### Releases

Download LuxuryGram only from this repository's [Releases](https://github.com/GofMan5/LuxuryGram/releases) page. Do not trust installers or archives that are not attached to a release there.

| Platform | Asset | How to run |
| --- | --- | --- |
| Windows x64 | `LuxuryGram-<version>-win-x64-portable.zip` | Unpack, run `LuxuryGram.exe` |
| Linux x64 | `LuxuryGram-<version>-linux-x64.tar.xz` | Unpack, run `LuxuryGram` |

Every release also ships `SHA256SUMS.txt`. Check your download before running it:

```bash
sha256sum -c SHA256SUMS.txt
```

```powershell
certutil -hashfile LuxuryGram-<version>-win-x64-portable.zip SHA256
```

The app updates itself: it checks this repository for a newer version and verifies the update signature with the LuxuryGram key before unpacking anything. The archives are not code-signed, so Windows SmartScreen may warn on first launch.

### Source

Clone the repository together with its submodules:

```bash
git clone --recursive https://github.com/GofMan5/LuxuryGram.git
cd LuxuryGram
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

## Build from source

Choose the guide for your platform:

- [Windows](docs/building-win.md)
- [macOS](docs/building-mac.md)
- [Linux with Docker](docs/building-linux.md)
- [Telegram API credentials](docs/api_credentials.md)

Telegram API credentials are required for self-built clients. Use your own credentials and never commit them to a public repository.

## Open-source project

| Resource | Purpose |
| --- | --- |
| [Contributing guide](CONTRIBUTING.md) | Set up a branch, build, test, and submit a focused change |
| [Code of Conduct](CODE_OF_CONDUCT.md) | Community standards |
| [Security policy](SECURITY.md) | Private vulnerability reporting and supported versions |
| [Support guide](SUPPORT.md) | Get help without exposing private data |
| [Changelog](CHANGELOG.md) | LuxuryGram-specific project history |
| [License](LICENSE) | GNU GPL v3 or later and the included OpenSSL linking exception |
| [Legal notice](LEGAL) | Copyright, attribution, and warranty terms |
| [Third-party notice](NOTICE.md) | Upstream projects and dependency licensing |

## Security and privacy

- Download only from this repository's release page, and verify the published checksum.
- Never post API hashes, phone numbers, authentication codes, session data, or unredacted logs in an issue.
- Report security vulnerabilities privately according to [`SECURITY.md`](SECURITY.md).
- Review code and build from source when your threat model requires independent verification.

No third-party Telegram client can eliminate the risks of account restrictions, upstream protocol changes, compromised devices, or unsafe plugins and builds.

## Upstream and credits

LuxuryGram exists thanks to the work of:

- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) — the feature-rich source line used as the starting point;
- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) — the upstream desktop client and fork network;
- all contributors and third-party projects listed in the source tree, submodules, and [`NOTICE.md`](NOTICE.md).

Telegram is a trademark of its respective owner. LuxuryGram does not claim ownership of the Telegram name or service.

## License

LuxuryGram is distributed under the GNU General Public License version 3 or, at your option, any later version, with the OpenSSL linking exception included in [`LICENSE`](LICENSE). Individual bundled components and submodules may use their own compatible licenses; their notices remain authoritative for those components.

---

<div align="center">
  <strong>LuxuryGram</strong><br>
  Built in the open, with respect for its upstream projects and users.
</div>

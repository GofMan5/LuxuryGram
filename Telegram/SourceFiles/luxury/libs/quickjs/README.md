# QuickJS-ng

Vendored JavaScript engine for LuxuryGram plugins (`luxury/plugins/js/`).

- Upstream: https://github.com/quickjs-ng/quickjs
- Version: tag `v0.17.0`, commit `6d46d07d04041b40f4f49eaa7fdebe44c314c699`
- License: MIT, see `LICENSE`
- Local patches: none. Every file is byte-identical to the upstream blob at
  that commit, including line endings.

Only the library is vendored: no `qjs`/`qjsc` command line tools, no
`quickjs-libc` (`std`/`os` modules), no tests.

Compiled sources: `quickjs.c`, `libregexp.c`, `libunicode.c`, `dtoa.c`.
Every other file is a header included by them.

To update, replace every file from the new tag, keep the list above in sync
and rerun `tools/tests/plugin_js_regressions.py`.

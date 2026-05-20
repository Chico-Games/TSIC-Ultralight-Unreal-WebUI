# TSIC Ultralight Unreal WebUI

> **THIS WILL NOT WORK OUT OF THE BOX.** This repo is a one-way snapshot of
> the Ultralight integration used by *The Store Is Closed* (TSIC), mirrored
> here as reference material for anyone trying to get Ultralight running in
> Unreal Engine. PRs in this repo will not auto-flow back upstream — please
> open issues or contact the author if you have improvements.

## What this is

A subset of the `TSICWebUI` plugin from the TSIC project:

- The full C++ wrapper module (`Source/TSICWebUI/`) — Ultralight renderer,
  GPU driver, Slate widget, UMG host, JS↔C++ message bridge, gamepad-focus
  helpers, texture provider, font loader, file system, log/listener glue.
- The third-party module declaration (`Source/ThirdParty/UltralightSDK/`)
  — the `Build.cs`, EULA and NOTICES files. **The SDK binaries (DLLs,
  static libs, headers, ICU data) are NOT included** — see "Getting the
  Ultralight SDK" below.
- A small, game-agnostic SPA runtime under `Web/shared/` — focus engine,
  bridge bootstrap, router, dropdown, context-menu, mock test-harness.
- The Playwright unit tests covering that runtime, under `Web/tests/`.

## What is intentionally NOT here

- TSIC-specific HTML screens (inventory, hotbar, settings, mods, etc.).
- TSIC-specific shared JS/CSS (`hud.js`, `inventory.js`, `storage-shell.js`,
  `modio*.js`, `lore.js`, `recipe-info.js`, `catalog.js`, `action-bar.js`,
  `catalogue.css`, `hud.css`).
- The TSIC test fixtures and playground.
- Ultralight SDK binaries (license requires per-user agreement — see below).

## Getting the Ultralight SDK

You must download the SDK separately and accept Ultralight Inc's EULA.

1. Visit https://ultralig.ht and follow their download / SDK instructions.
2. Drop the headers into `Source/ThirdParty/UltralightSDK/include/`.
3. Drop the import libs (`Ultralight.lib`, `UltralightCore.lib`, `WebCore.lib`)
   into `Source/ThirdParty/UltralightSDK/Win64/`.
4. Drop the runtime DLLs and `resources/` (icudt67l.dat, cacert.pem) into
   `Source/ThirdParty/UltralightSDK/Binaries/Win64/`.

The `UltralightSDK.Build.cs` in this repo expects exactly that layout.

## Names ship with the `TSIC` prefix

Classes, the JS namespace, and meta tags use `TSIC`/`tsic` (`UTSICWebUISubsystem`,
`STSICWebViewSlate`, `window.tsic`, `<meta name="tsic-screen">`). This is
intentional — the mirror is reference code, not a drop-in plugin. If you
adopt it, expect to rename.

## Engine version

Built against Unreal Engine **5.6**. Earlier versions are not tested.

## License

The code in this repository (everything *except* the Ultralight SDK itself
once you install it) is MIT-licensed — see `LICENSE`. The Ultralight SDK is
governed by Ultralight Inc's EULA, included in
`Source/ThirdParty/UltralightSDK/EULA.txt`.

## Upstream

This repo is automatically updated from a private upstream project. Commits
here are squashed snapshots — the public log is the sync log, not the full
authoring history.

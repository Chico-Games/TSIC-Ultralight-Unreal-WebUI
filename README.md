# TSIC Ultralight Unreal WebUI — reference, not a plugin

**This is not a drop-in plugin.** It's a snapshot of how I integrated
[Ultralight](https://ultralig.ht) into Unreal Engine 5.6 for my game
*The Store Is Closed* (TSIC). I'm publishing it as a reference for
anyone trying to do the same thing — to read, learn from, copy bits
of into their own code.

**What you should expect:**

- It will not compile if you just clone it. The Ultralight SDK is not
  redistributed here (license requires per-user agreement — see below).
- Class names, file names, the JS namespace, and meta tags all carry
  the `TSIC` / `tsic` prefix. They are not parameterised. If you reuse
  this code in a real project, you will rename things.
- Some pieces are intentionally missing — anything specific to TSIC's
  gameplay (inventory pages, HUD scripts, mods integration) is filtered
  out. Only the engine-side wrapper and the game-agnostic SPA runtime
  are mirrored here.
- No support is offered. I'm not maintaining this as a product. Issues
  and PRs are welcome but I make no promises about responding to them.

If you want a turn-key Ultralight + UE plugin, look elsewhere — there
are several on GitHub. This repo is for people who want to see one
working example end-to-end.

---

## What is here

- **`Source/TSICWebUI/`** — the C++ wrapper module: Ultralight renderer,
  custom GPU driver against UE's RHI, Slate widget, UMG host, JS↔C++
  message bridge, gamepad-focus helpers, texture provider, font loader,
  file system, log/listener glue.
- **`Source/ThirdParty/UltralightSDK/`** — the `Build.cs` declaration,
  Ultralight's EULA and NOTICES. **No SDK headers, libs, or DLLs are
  included.** You supply those yourself.
- **`Web/shared/`** — game-agnostic SPA pieces: a focus engine for
  controller/keyboard navigation, the JS bridge bootstrap (`tsic-runtime`),
  a router, a dropdown component, a context-menu helper, a mock
  bridge for testing pages outside of UE.
- **`Web/tests/`** — Playwright unit tests covering the runtime above.

## What is not here

Filtered out by the sync because they're TSIC-specific:

- HTML pages in `Content/UI/Web/screens/` — inventory, hotbar, mods,
  settings, save/load, all of TSIC's actual UI.
- The shared JS/CSS that backs those pages — `hud.js`, `inventory.js`,
  `storage-shell.js`, `modio*.js`, `lore.js`, `recipe-info.js`,
  `catalog.js`, `action-bar.js`, `catalogue.css`, `hud.css`.
- The TSIC playground / test fixtures.
- The full Playwright test suite (only the runtime tests come along).

## Getting the Ultralight SDK

You need the SDK to build this. Get it from Ultralight directly and
accept their EULA.

1. Visit https://ultralig.ht and follow their download instructions.
2. Drop the headers into `Source/ThirdParty/UltralightSDK/include/`.
3. Drop the import libs (`Ultralight.lib`, `UltralightCore.lib`,
   `WebCore.lib`) into `Source/ThirdParty/UltralightSDK/Win64/`.
4. Drop the runtime DLLs and `resources/` (`icudt67l.dat`,
   `cacert.pem`) into `Source/ThirdParty/UltralightSDK/Binaries/Win64/`.

The `UltralightSDK.Build.cs` here expects exactly that layout.

## Engine version

Built against **Unreal Engine 5.6** with the `WITH_ULTRALIGHT`/standard
RHI path. Earlier versions are untested.

## License

The code in this repository (everything *except* the Ultralight SDK
itself once you install it) is MIT-licensed — see `LICENSE`. The
Ultralight SDK is governed by Ultralight Inc's EULA, included in
`Source/ThirdParty/UltralightSDK/EULA.txt`.

## How this repo is updated

This is a one-way mirror. Commits here are produced by a GitHub Action
running on the upstream (private) project — each commit is a squashed
snapshot named `Sync from TSIC @ <sha>: <subject>`. The public log
here is the sync log, not the authoring history.

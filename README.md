# TSIC Ultralight Unreal WebUI — reference, not a plugin

How I integrated [Ultralight](https://ultralig.ht) into Unreal Engine 5.6
for *The Store Is Closed*. Reference code, not a drop-in plugin. It will
not compile on clone, names carry the `TSIC` prefix, and the SDK is not
redistributed here. No support offered.

## Getting the Ultralight SDK

Download from https://ultralig.ht (their SDK, their EULA). Then place
files into `Source/ThirdParty/UltralightSDK/`:

| Ultralight file | Goes in |
|---|---|
| `include/` (headers) | `Source/ThirdParty/UltralightSDK/include/` |
| `Ultralight.lib`, `UltralightCore.lib`, `WebCore.lib` | `Source/ThirdParty/UltralightSDK/Win64/` |
| `Ultralight.dll`, `UltralightCore.dll`, `WebCore.dll` | `Source/ThirdParty/UltralightSDK/Binaries/Win64/` |
| `resources/icudt67l.dat`, `resources/cacert.pem` | `Source/ThirdParty/UltralightSDK/Binaries/Win64/resources/` |

`UltralightSDK.Build.cs` expects exactly that layout.

## What each piece does

**C++ (`Source/TSICWebUI/`)**

| File pair | Purpose |
|---|---|
| `TSICWebUI` | Module entry. Owns the Ultralight platform runtime (renderer, file system, font loader, GPU driver) so it survives PIE start/stop. |
| `TSICWebUISubsystem` | GameInstance subsystem. Creates and manages views, exposes the public API for the game side. |
| `TSICWebView` | Wraps a single Ultralight `View`. |
| `STSICWebViewSlate` | Slate widget that renders a view's texture and routes input into it. |
| `TSICWebUIViewportWidget` | UMG widget that hosts the Slate widget so designers can drop it into a UMG layout. |
| `TSICWebGPUDriver` + `TSICWebShaders` | Implements Ultralight's `GPUDriver` against UE's RHI. Custom shaders for path/rect/glyph rendering. |
| `TSICWebFileSystem` | Ultralight `FileSystem` impl backed by UE's `FFileHelper`. Resolves `file:///` URLs against the plugin's `Content/UI/Web/`. |
| `TSICWebFontLoader` | Ultralight `FontLoader` impl using UE's font resolution. |
| `TSICWebLogger` + `TSICWebLoadListener` + `TSICWebViewListener` | Bridges Ultralight log/load/view callbacks into `UE_LOG` and Unreal events. |
| `TSICWebJSBindings` | Installs `window.tsic` and marshals JS ↔ C++ calls (channels, JSON, struct support). |
| `TSICWebMessageBridge` | Typed channel registry. Maps gameplay tags to JSON payloads; supports sticky caching + replay on context-ready. |
| `TSICWebEventBus` | Pub/sub event bus with sticky cache. |
| `TSICWebTexRegistry` + `TSICWebTexProvider` | Lets HTML reference runtime UE textures as `<img src="tex:...">`. |
| `TSICWebKeyMappings` | Translates UE input codes to Ultralight key codes. |

**Web runtime (`Web/shared/`)**

| File | Purpose |
|---|---|
| `tsic-runtime.js` | `window.tsic` bootstrap: `whenReady`, message helpers, sticky-cache replay. |
| `tsic-focus.js` | Gamepad/keyboard focus engine. Spatial nearest-in-direction, modal scopes, focus memory. |
| `tsic-dropdown.js` | Dropdown component. |
| `context-menu.js` | Right-click / long-press context menu helper. |
| `router.js` | Self-navigating router. Each page declares its screen via `<meta name="tsic-screen">`; swaps on `UI.Screen.Changed`. |
| `test-harness.js` | Mock bridge for running pages in a browser without UE. |
| `base.css` / `components.css` / `tsic-ui.css` | Reset + design tokens, component styles, utility classes. |

**Tests (`Web/tests/`)** — Playwright unit tests for the runtime above.

## License

Repository code: MIT (see `LICENSE`). Ultralight SDK: Ultralight Inc's
EULA (see `Source/ThirdParty/UltralightSDK/EULA.txt`).

## How this repo is updated

One-way mirror from a private upstream. Each commit is a squashed
snapshot `Sync from TSIC @ <sha>`.

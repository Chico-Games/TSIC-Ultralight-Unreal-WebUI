// Self-navigating router. Each page declares its own screen name via:
//   <meta name="tsic-screen" content="MainMenu">
// On UI.Screen.Changed, navigate if the broadcast name differs from this page's.
//
// HUD overlays (action-bar, hotbar, health-bar, etc.) listed in OVERLAY_SCREENS
// are exempt from the redirect — they coexist with the active main screen and
// must observe UI.Screen.Changed without navigating themselves away.
//
// Also reads:
//   <meta name="tsic-input-mode" content="InputMode.Menu.Map">       (existing)
//   <meta name="tsic-action-bar-context" content='[ {...}, ... ]'>   (this spec)
//   <meta name="tsic-cancel-cmd"   content="UI.Cmd.Settings.Back">    (cancel/back command)
//   <meta name="tsic-debug" content="true">                           (opt out of director routing)
//
// Debug / out-of-band pages reached via console command or direct LoadURL
// (debug grids, test harness, playground) declare tsic-debug=true so the
// sticky UI.Screen.Changed broadcast doesn't instantly redirect them back
// to the active in-game screen on mount. They still participate in input
// mode, cancel/back, and action-bar wiring as normal.
//
// Cancel/back: pages that push an input-mode tag automatically wire ESC /
// IA_UI_CancelBack to publish a close command. Default is UI.Cmd.Pause.Resume
// (which the director routes to SetScreen(InGame) + unpause). Override via
// the tsic-cancel-cmd meta tag when a screen needs different back semantics
// (Settings → UI.Cmd.Settings.Back, etc.).
(function () {
  // HUD overlay screens never redirect on Screen.Changed — they're meant to
  // sit on top of whichever main screen is active and react to the broadcast
  // (e.g. action-bar swaps between gameplay/menu group, hotbar follows the
  // selected slot). A redirect here breaks the overlay entirely.
  const OVERLAY_SCREENS = new Set([
    'ActionBar', 'Hotbar', 'HealthBar', 'StaminaBar', 'Stomach',
    'Crosshair', 'Detection', 'CircularProgress', 'Notifications',
    'PlayerIndicators', 'Ping', 'PingMarkers',
  ]);
  const SCREEN_TO_FILE = {
    MainMenu: 'main-menu',
    NewStore: 'new-store',
    Mods: 'mods',
    Credits: 'credits',
    Loading: 'loading-screen',
    InGame: 'in-game',
    DeathScreen: 'death-screen',
    PauseMenu: 'pause-menu',
    Inventory: 'inventory',
    Settings: 'settings',
    SaveLoad: 'save-load',
    Crafting: 'crafting',
    Production: 'production',
    Upgrade: 'upgrade',
    BossSummoner: 'boss-summoner',
    Construction: 'construction',
    Interaction: 'interaction',
    Selection: 'selection',
    Cage: 'cage',
    Notifications: 'notifications',
    Map: 'map',
    Chat: 'chat',
    Teleporter: 'teleporter',
    BugReport: 'bug-report',
    Storage: 'storage',
    Repair: 'repair',
    UniversalStorage: 'universal-storage',
    UniversalStorageSetup: 'universal-storage-setup',
    CheatMenu: 'cheat-menu',
    VoiceChat: 'voice-chat',
    PlayerIndicators: 'player-indicators',
    PingMarkers: 'ping-markers',
    CircularProgress: 'circular-progress',
    ConstructionCarousel: 'construction-carousel',
    Paper: 'paper',
    Screen: 'screen',
    Tablet: 'tablet',
    Tests: 'tests',
    Equipment: 'equipment',
    Wardrobe: 'wardrobe',
    Hotbar: 'hotbar',
    QuantityPicker: 'quantity-picker',
    ActionBar: 'action-bar',
    Ping: 'ping',
    Crosshair: 'crosshair',
    Detection: 'detection',
    HealthBar: 'health-bar',
    StaminaBar: 'stamina-bar',
    Stomach: 'stomach',
  };

  function myScreen() {
    const m = document.querySelector('meta[name="tsic-screen"]');
    return m ? m.getAttribute('content') : '';
  }

  function fileFor(name) {
    return SCREEN_TO_FILE[name] || null;
  }

  function whenReady(cb) {
    if (window.tsic) { cb(); return; }
    setTimeout(() => whenReady(cb), 16);
  }

  function activeInputModeTag() {
    const m = document.querySelector('meta[name="tsic-input-mode"]');
    return m ? m.getAttribute('content') : null;
  }

  function isDebugScreen() {
    const m = document.querySelector('meta[name="tsic-debug"]');
    return !!m && m.getAttribute('content') === 'true';
  }

  function cancelCmd() {
    const m = document.querySelector('meta[name="tsic-cancel-cmd"]');
    const raw = m ? m.getAttribute('content') : null;
    if (raw && raw.trim().length > 0) return raw.trim();
    // Debug pages default to UI.Cmd.Debug.Close so the director picks
    // MainMenu vs InGame based on the loaded world instead of forcing
    // Pause.Resume (which always routes to InGame and would strand a
    // user who opened the debug page from the main menu).
    if (isDebugScreen()) return 'UI.Cmd.Debug.Close';
    return 'UI.Cmd.Pause.Resume';
  }

  function staticActionBarContext() {
    const m = document.querySelector('meta[name="tsic-action-bar-context"]');
    if (!m) return null;
    const raw = m.getAttribute('content');
    if (!raw) return null;
    try {
      const parsed = JSON.parse(raw);
      return Array.isArray(parsed) ? parsed : null;
    } catch (err) {
      console.warn('[router] failed to parse tsic-action-bar-context:', err);
      return null;
    }
  }

  // Single point of truth for sending menu context to C++. De-dupes by
  // ActionName and ensures an IA_UI_CancelBack "Back" row is present unless
  // the page declared its own.
  function publishMenuContext(entries) {
    const seen = new Set();
    const out = [];
    for (const e of (entries || [])) {
      if (!e || !e.ActionName || seen.has(e.ActionName)) continue;
      seen.add(e.ActionName);
      out.push({
        ActionName: String(e.ActionName),
        Label:      String(e.Label || ''),
        Priority:   Number.isFinite(e.Priority) ? e.Priority : 100,
      });
    }
    if (!seen.has('IA_UI_CancelBack')) {
      out.push({ ActionName: 'IA_UI_CancelBack', Label: 'Back', Priority: 1000 });
    }
    if (window.tsic && window.tsic.publishMessage) {
      window.tsic.publishMessage('UI.Cmd.ActionBar.SetMenuContext', { Entries: out });
    }
  }
  // Exposed early so shared/action-bar.js can route through us.
  window.__tsicPublishMenuActionContext = publishMenuContext;

  whenReady(() => {
    // Screen routing.
    window.tsic.on('tsic.msg.UI.Screen.Changed', (payload /*, meta, name*/) => {
      if (!payload || !payload.Name) return;
      if (payload.Name === myScreen()) return;
      // Overlays observe Screen.Changed but never navigate themselves.
      if (OVERLAY_SCREENS.has(myScreen())) return;
      // Debug / out-of-band pages opt out via <meta name="tsic-debug" content="true">.
      // UI.Screen.Changed is sticky, so without this guard the bridge would
      // replay the active in-game screen on mount and immediately redirect
      // the debug page away (the "WebUI.Map.DebugGrid closing on open" bug).
      if (isDebugScreen()) return;
      const file = fileFor(payload.Name);
      if (!file) {
        console.warn('[router] no file mapping for screen', payload.Name);
        return;
      }
      window.location.replace(`/screens/${file}.html`);
    });

    // Input-mode tag activation: append on load, release on page teardown.
    const inputTag = activeInputModeTag();
    if (inputTag) {
      window.tsic.publishMessage('UI.Cmd.Input.AppendModeTag', { Tag: inputTag });
      let inputTagRemoved = false;
      const removeInputTagOnce = () => {
        if (inputTagRemoved) return;
        inputTagRemoved = true;
        window.tsic.publishMessage('UI.Cmd.Input.RemoveModeTag', { Tag: inputTag });
      };
      window.addEventListener('beforeunload', removeInputTagOnce);
      window.addEventListener('pagehide', removeInputTagOnce);

      // IA_UI_CancelBack auto-wiring: only on screens that push a menu input
      // tag (so IMC_CancelBack is actually active here). Fires regardless of
      // WebView focus state — the bridge publishes the IA event over IPC,
      // not via the keyboard event loop. Pages may opt out by setting the
      // meta to an empty string, or override the command via tsic-cancel-cmd.
      const cmd = cancelCmd();
      if (cmd) {
        window.tsic.on('tsic.msg.UI.Input.IA_UI_CancelBack', (p) => {
          if (!p || p.Phase !== 'Started') return;
          window.tsic.publishMessage(cmd, {});
        });
      }
    }

    // Modder API helpers — bound on window.tsic after the native bootstrap.
    window.tsic.appendInputModeTag = function (tagStr) {
      window.tsic.publishMessage('UI.Cmd.Input.AppendModeTag', { Tag: tagStr });
    };
    window.tsic.removeInputModeTag = function (tagStr) {
      window.tsic.publishMessage('UI.Cmd.Input.RemoveModeTag', { Tag: tagStr });
    };

    // Menu action-bar wiring.
    //
    // Pages that own a static, page-wide context use the meta tag below; pages
    // with dynamic context (Inventory) call tsic.setMenuActionContext from
    // their own JS, which routes through the same publishMenuContext helper.
    //
    // The auto-Back row is injected by publishMenuContext.
    const staticCtx = staticActionBarContext();
    if (staticCtx !== null) {
      // Page declared either an empty array (auto-Back only) or a list.
      publishMenuContext(staticCtx);
    }
    // Always clear on teardown so the next page doesn't briefly see ours.
    // (Truly bar-less pages — Loading, MainMenu — leave the meta tag off; the
    // bar's own visibility gate hides it for non-menu screens anyway, but
    // clearing here keeps the sticky cache tidy.)
    if (staticCtx !== null) {
      let cleared = false;
      const clearOnce = () => {
        if (cleared) return;
        cleared = true;
        window.tsic.publishMessage('UI.Cmd.ActionBar.SetMenuContext', { Entries: [] });
      };
      window.addEventListener('beforeunload', clearOnce);
      window.addEventListener('pagehide', clearOnce);
    }
  });

  // Expose for ad-hoc dev navigation.
  window.tsicGoto = function (name) {
    const file = fileFor(name);
    if (file) window.location.replace(`/screens/${file}.html`);
  };
})();

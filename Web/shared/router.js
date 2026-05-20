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

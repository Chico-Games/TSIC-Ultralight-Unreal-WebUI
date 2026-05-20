// shared/tsic-runtime.js
//
// Tiny utility belt that every SPA page can rely on. The intent is to give
// pages a uniform "boot" surface so each page only has to declare its data
// subscriptions + DOM interactions — no repeated whenReady polling, no
// hand-rolled Esc handlers, no copy-pasted Pause.Resume publishes.
//
// Everything is attached to `window.tsic` (the existing namespace stamped by
// the C++ bridge OR by the test-harness mock). The shim ALWAYS waits for the
// bridge to load before installing, so the production C++ tsic and the test
// mock both work without races.
(function () {
    function install(t) {
        if (t.__runtimeInstalled) return;
        t.__runtimeInstalled = true;

        // ---- Bootstrap helpers ------------------------------------------
        // Resolves once window.tsic exists. Pages call this exactly once.
        t.whenReady = t.whenReady || function (cb) {
            if (window.tsic) { try { cb(); } catch (e) { console.warn('[tsic.whenReady]', e); } return; }
            setTimeout(() => t.whenReady(cb), 16);
        };

        // Subscribe to a sticky-cached channel only after the bridge is ready.
        // Equivalent to: whenReady(() => tsic.on(channel, cb)).
        t.onReady = function (channel, cb) {
            t.whenReady(() => t.on(channel, cb));
        };

        // ---- Outbound message shortcuts -------------------------------
        t.resume       = () => t.publishMessage('UI.Cmd.Pause.Resume', {});
        t.quitToMenu   = () => t.publishMessage('UI.Cmd.Pause.QuitToMenu', {});
        t.closeScreen  = () => t.publishMessage('UI.Cmd.GameScreen.Close', {});
        t.playSound    = (key, vol) => t.publishMessage('UI.Cmd.Sound.Play',
            { SoundKey: key, VolumeScale: typeof vol === 'number' ? vol : 1.0 });

        // ---- DOM helpers ------------------------------------------------
        t.qs  = (sel, root) => (root || document).querySelector(sel);
        t.qsa = (sel, root) => Array.from((root || document).querySelectorAll(sel));

        // Bind a click handler to a selector or element. Returns an unbind fn.
        t.onClick = function (target, fn) {
            const el = (typeof target === 'string') ? t.qs(target) : target;
            if (!el) return () => {};
            const handler = (ev) => { try { fn(ev); } catch (e) { console.warn('[onClick]', e); } };
            el.addEventListener('click', handler);
            return () => el.removeEventListener('click', handler);
        };

        // Bind any key on the window to a handler.
        t.onKey = function (key, fn, opts) {
            const o = opts || {};
            const handler = (ev) => {
                if (ev.key !== key) return;
                if (o.requireFocus && document.activeElement && document.activeElement !== document.body) {
                    // Skip if focus is in an input/textarea — useful for global Esc handlers.
                    const tn = document.activeElement.tagName;
                    if (tn === 'INPUT' || tn === 'TEXTAREA' || document.activeElement.isContentEditable) return;
                }
                try { fn(ev); } catch (e) { console.warn('[onKey]', e); }
            };
            window.addEventListener('keydown', handler, o.capture === true);
            return () => window.removeEventListener('keydown', handler, o.capture === true);
        };

        // Wires Esc to the page's idiomatic close behaviour:
        //   - default: publish UI.Cmd.Pause.Resume
        //   - opts.closeScreen: publish UI.Cmd.GameScreen.Close instead
        //   - opts.handler: invoke a custom callback
        t.bindEscape = function (opts) {
            const o = opts || {};
            return t.onKey('Escape', () => {
                if (o.handler) { o.handler(); return; }
                if (o.closeScreen) { t.closeScreen(); return; }
                t.resume();
            });
        };

        // Wires a close-button (#btn-close by default) to the same behaviour.
        t.bindCloseButton = function (selectorOrEl, opts) {
            return t.onClick(selectorOrEl || '#btn-close', () => {
                const o = opts || {};
                if (o.handler) { o.handler(); return; }
                if (o.closeScreen) { t.closeScreen(); return; }
                t.resume();
            });
        };

        // ---- Page boot helper ------------------------------------------
        // Standard recipe for menu screens: wait for the bridge, run setup,
        // wire Esc + the close button.
        //
        //   tsic.bootMenu(({ on }) => {
        //     on('tsic.msg.UI.Foo.Bar', (p) => renderFoo(p));
        //   });
        t.bootMenu = function (setup, opts) {
            const o = opts || {};
            t.whenReady(() => {
                const ctx = {
                    on: (ch, cb) => t.on(ch, cb),
                    publish: (ch, p) => t.publishMessage(ch, p || {}),
                };
                try { setup && setup(ctx); } catch (e) { console.warn('[bootMenu]', e); }
                if (o.escape !== false)     t.bindEscape({ closeScreen: !!o.closeScreen, handler: o.onClose });
                if (o.closeButton !== false) t.bindCloseButton(o.closeButton || '#btn-close',
                    { closeScreen: !!o.closeScreen, handler: o.onClose });
            });
        };

        // ---- Item-icon URL helper (used everywhere we render an item) ----
        // Falls back to the catalog's iconIconUrl when present, otherwise the
        // built-in tex:// scheme. Pages should call this instead of building
        // tex:// strings inline.
        t.iconUrlFor = function (itemId) {
            if (!itemId) return null;
            if (typeof t.itemIconUrl === 'function') return t.itemIconUrl(itemId);
            return `tex://item-icon/${encodeURIComponent(itemId)}`;
        };
    }

    // The mock tsic in test-harness.js installs the namespace synchronously
    // before any page script runs; production C++ stamps it slightly later.
    // Either way, poll-and-install — this stays harmless if called twice.
    (function poll() {
        if (window.tsic) { install(window.tsic); return; }
        setTimeout(poll, 16);
    })();
})();

// shared/tsic-focus.js
//
// Focus engine for the SPA. Activates on pages that opt in via
//   <meta name="tsic-focus" content="enabled">
// Mirrors UI.Input.Mode.Changed onto <html data-tsic-input="..."> so CSS can
// branch on input mode regardless of whether the engine is active. When the
// page opts in, the engine takes over keyboard/D-pad navigation: spatial
// nearest-in-direction picks the next focus target, the ring is rendered via
// [data-tsic-focused], scrollable containers auto-follow, and modal scopes
// constrain navigation + restore caller focus on close.
//
// Exposes window.tsic.focus.*:
//   enable() / disable() / isEnabled() / getMode()
//   refresh()                      // re-scan after dynamic re-render (currently a no-op
//                                  // because the focusable set is read fresh per step)
//   focus(elOrSel)                 // programmatically set focus
//   step(dir)                      // 'up'|'down'|'left'|'right' — spatial nearest
//   pushScope(rootEl, initialEl)   // modal scope; subsequent step() restricted to root
//   popScope()                     // restores caller focus
//   resetMemory()                  // clear per-screen focus memory (for tests)
//   snapshot()                     // debug — { mode, enabled, scope, focusable: [...] }
//   __focusableSet()               // (test-only) returns current focusable list
//   __stableSelector(el)           // (test-only) structural selector
//   __state                        // (test-only) internal state object
(function () {
    function install(t) {
        if (t.__focusInstalled) return;
        t.__focusInstalled = true;

        const State = {
            enabled: false,
            mode: 'MouseAndKeyboard',
            memory: {},          // per-screen last-focused stable selector
            scopeStack: [],      // [{ root, caller }]
            smoothScroll: true,  // tests flip this to 'instant'
        };

        // ---- DOM helpers --------------------------------------------------

        function stampMode(mode) {
            try { document.documentElement.setAttribute('data-tsic-input', mode); }
            catch (e) { /* document might not be ready in odd test cases */ }
        }

        function metaSaysEnabled() {
            const m = document.querySelector('meta[name="tsic-focus"]');
            return !!(m && m.getAttribute('content') === 'enabled');
        }

        function isFocusable(el) {
            if (!el || el.nodeType !== 1) return false;
            if (el.matches('[hidden], [disabled], [aria-hidden="true"], [data-tsic-skip-focus]')) return false;
            if (!el.matches('button, a[href], input:not([type=hidden]), select, textarea, [data-tsic-focusable]')) return false;
            const r = el.getBoundingClientRect();
            if (r.width === 0 || r.height === 0) return false;
            return true;
        }

        // Like isFocusable but ignores zero-rect filter. Used when a scope was
        // just pushed and elements haven't laid out yet, or in environments
        // without real layout (jsdom in tests).
        function isStructurallyFocusable(el) {
            if (!el || el.nodeType !== 1) return false;
            if (el.matches('[hidden], [disabled], [aria-hidden="true"], [data-tsic-skip-focus]')) return false;
            return el.matches('button, a[href], input:not([type=hidden]), select, textarea, [data-tsic-focusable]');
        }

        function focusableSet(root) {
            const scope = root || document;
            return Array.from(scope.querySelectorAll(
                'button, a[href], input:not([type=hidden]), select, textarea, [data-tsic-focusable]'
            )).filter(isFocusable);
        }

        // DOM-order list (no rect filter). Used as a fallback for spatial nav
        // when no candidate has a measurable rect — e.g. inside a fresh portal
        // before layout or in a test environment.
        function structuralFocusableSet(root) {
            const scope = root || document;
            return Array.from(scope.querySelectorAll(
                'button, a[href], input:not([type=hidden]), select, textarea, [data-tsic-focusable]'
            )).filter(isStructurallyFocusable);
        }

        function screenKey() {
            const m = document.querySelector('meta[name="tsic-screen"]');
            return m ? m.getAttribute('content') : 'Unknown';
        }

        function stableSelector(el) {
            if (!el) return null;
            if (el.id) return '#' + el.id;
            if (el.dataset && el.dataset.tsicFocusId) {
                return '[data-tsic-focus-id="' + el.dataset.tsicFocusId + '"]';
            }
            const group = el.closest('[data-tsic-focus-group]') || document.body;
            const path = [];
            let n = el;
            while (n && n !== group && n.parentNode) {
                const sib = Array.from(n.parentNode.children).filter(c => c.tagName === n.tagName);
                path.unshift(n.tagName.toLowerCase() + ':nth-of-type(' + (sib.indexOf(n) + 1) + ')');
                n = n.parentNode;
            }
            const prefix = (group.dataset && group.dataset.tsicFocusGroup)
                ? '[data-tsic-focus-group="' + group.dataset.tsicFocusGroup + '"] '
                : '';
            return prefix + path.join(' > ');
        }

        function findInitial() {
            return document.querySelector('[data-tsic-initial-focus]');
        }

        // ---- Spatial-nearest ---------------------------------------------

        function centre(rect) { return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 }; }

        function pickNeighbor(from, dir, candidates) {
            if (!from) return null;
            const fr = from.getBoundingClientRect();
            const fc = centre(fr);
            let best = null;
            let bestScore = Infinity;
            let bestIdx = Infinity;
            candidates.forEach((c, idx) => {
                if (c === from) return;
                const cr = c.getBoundingClientRect();
                const cc = centre(cr);
                let dirDist = 0, perpOffset = 0, inHalfPlane = false;
                switch (dir) {
                    case 'up':    inHalfPlane = cc.y < fc.y - 1; dirDist = fc.y - cc.y; perpOffset = Math.abs(cc.x - fc.x); break;
                    case 'down':  inHalfPlane = cc.y > fc.y + 1; dirDist = cc.y - fc.y; perpOffset = Math.abs(cc.x - fc.x); break;
                    case 'left':  inHalfPlane = cc.x < fc.x - 1; dirDist = fc.x - cc.x; perpOffset = Math.abs(cc.y - fc.y); break;
                    case 'right': inHalfPlane = cc.x > fc.x + 1; dirDist = cc.x - fc.x; perpOffset = Math.abs(cc.y - fc.y); break;
                }
                if (!inHalfPlane) return;
                const score = dirDist + perpOffset * 3;
                if (score < bestScore || (score === bestScore && idx < bestIdx)) {
                    best = c;
                    bestScore = score;
                    bestIdx = idx;
                }
            });
            return best;
        }

        // ---- Scroll into view --------------------------------------------

        function scrollFocusIntoView(el) {
            let n = el.parentElement;
            let container = null;
            while (n && n !== document.body) {
                const cs = getComputedStyle(n);
                if ((cs.overflowY === 'auto' || cs.overflowY === 'scroll') && n.scrollHeight > n.clientHeight) {
                    container = n; break;
                }
                n = n.parentElement;
            }
            if (!container) return;
            const margin = (el.getBoundingClientRect().height || 28) * 1.5;
            const cRect = container.getBoundingClientRect();
            const eRect = el.getBoundingClientRect();
            const behavior = (State.smoothScroll === false) ? 'instant' : 'smooth';
            if (eRect.top - margin < cRect.top) {
                container.scrollBy({ top: (eRect.top - margin) - cRect.top, behavior: behavior });
            } else if (eRect.bottom + margin > cRect.bottom) {
                container.scrollBy({ top: (eRect.bottom + margin) - cRect.bottom, behavior: behavior });
            }
        }

        // ---- Public API --------------------------------------------------

        const api = {
            enable() {
                State.enabled = true;
                if (State.mode === 'Gamepad') applyInitialFocus();
            },
            disable() { State.enabled = false; },
            isEnabled() { return State.enabled; },
            getMode() { return State.mode; },

            refresh() {
                // No cached state — focusable list is read fresh on every step.
                // Hook exists so pages can call after re-rendering without
                // changing call sites if we add caching later.
            },

            focus(elOrSel, opts) {
                const el = (typeof elOrSel === 'string') ? document.querySelector(elOrSel) : elOrSel;
                if (!el) return false;
                // opts.trust: skip rect-based isFocusable. Used by pushScope
                // when the caller explicitly names the initial element (e.g.
                // dropdown portals whose <li>s haven't been laid out yet).
                if (!(opts && opts.trust) && !isFocusable(el)) return false;
                try { el.focus({ preventScroll: true }); } catch (e) { try { el.focus(); } catch (_) {} }
                for (const stale of document.querySelectorAll('[data-tsic-focused]')) {
                    if (stale !== el) stale.removeAttribute('data-tsic-focused');
                }
                el.setAttribute('data-tsic-focused', '');
                const sel = stableSelector(el);
                if (sel) State.memory[screenKey()] = sel;
                try { scrollFocusIntoView(el); } catch (e) {}
                return true;
            },

            step(dir) {
                if (!State.enabled || State.mode !== 'Gamepad') return false;
                const scopeRoot = State.scopeStack.length > 0
                    ? State.scopeStack[State.scopeStack.length - 1].root
                    : document;
                let candidates = focusableSet(scopeRoot);
                // Layout-less fallback: nothing has a measurable rect (fresh
                // portal, or jsdom-style test env). Use the structural list
                // and DOM-order navigation instead of spatial-nearest.
                const layoutless = candidates.length === 0;
                if (layoutless) candidates = structuralFocusableSet(scopeRoot);
                if (candidates.length === 0) return false;
                const cur = document.activeElement;
                if (!cur || !candidates.includes(cur)) {
                    const init = findInitial();
                    if (init && candidates.includes(init)) return api.focus(init, { trust: layoutless });
                    return api.focus(candidates[0], { trust: layoutless });
                }
                if (layoutless) {
                    // DOM order. Up/Left = previous, Down/Right = next.
                    const idx = candidates.indexOf(cur);
                    const delta = (dir === 'up' || dir === 'left') ? -1 : 1;
                    const next = candidates[idx + delta];
                    if (!next) return false;
                    return api.focus(next, { trust: true });
                }
                const next = pickNeighbor(cur, dir, candidates);
                if (!next) return false;
                return api.focus(next);
            },

            pushScope(root, initial, opts) {
                if (!root) return false;
                const caller = document.activeElement;
                const onPop = (opts && typeof opts.onPop === 'function') ? opts.onPop : null;
                State.scopeStack.push({ root: root, caller: caller, onPop: onPop });
                const target = (typeof initial === 'string') ? root.querySelector(initial) : initial;
                if (target) {
                    // Caller explicitly named the initial target — trust it and
                    // focus without re-running the rect-based isFocusable check.
                    // This matters for portals (the dropdown's <li> options)
                    // whose layout hasn't been measured yet.
                    api.focus(target, { trust: true });
                } else {
                    const found = focusableSet(root)[0];
                    if (found) api.focus(found);
                }
                return true;
            },

            popScope() {
                if (State.scopeStack.length === 0) return false;
                const frame = State.scopeStack.pop();
                // Let the scope owner (e.g. tsic-dropdown) clean up its DOM.
                if (frame.onPop) {
                    try { frame.onPop(); } catch (e) { console.warn('[tsic-focus] onPop threw', e); }
                }
                if (frame.caller && isFocusable(frame.caller)) api.focus(frame.caller);
                return true;
            },

            resetMemory() { State.memory = {}; },

            snapshot() {
                return {
                    mode: State.mode,
                    enabled: State.enabled,
                    scope: State.scopeStack.length,
                    focusable: focusableSet().length,
                };
            },

            // Test-only escape hatches.
            __focusableSet: focusableSet,
            __structuralFocusableSet: structuralFocusableSet,
            __stableSelector: stableSelector,
            __state: State,
        };

        function applyInitialFocus() {
            const saved = State.memory[screenKey()];
            if (saved) {
                let restored = null;
                try { restored = document.querySelector(saved); } catch (e) { restored = null; }
                if (restored && isStructurallyFocusable(restored)) {
                    api.focus(restored, { trust: true });
                    return;
                }
            }
            const init = findInitial();
            // Trust the author's declaration — initial-focus is the canonical
            // landing element. We accept it even if its rect hasn't been
            // measured yet (page just rendered, layout-less env, etc.).
            if (init && isStructurallyFocusable(init)) {
                api.focus(init, { trust: true });
                return;
            }
            // Last resort: the first focusable in the layout if there is one,
            // else the first structurally focusable so navigation can start.
            const first = focusableSet()[0] || structuralFocusableSet()[0];
            if (first) api.focus(first, { trust: true });
        }

        t.focus = api;

        // ---- Channel wiring ----------------------------------------------

        t.on('tsic.msg.UI.Input.Mode.Changed', (payload) => {
            const mode = (payload && payload.Mode) || 'MouseAndKeyboard';
            State.mode = mode;
            stampMode(mode);
            if (!State.enabled) return;
            if (mode === 'Gamepad') {
                try { t.setInteractiveRects && t.setInteractiveRects([]); } catch (e) {}
                applyInitialFocus();
            } else {
                // Restore default (whole-view interactive) for the page's mouse
                // mode. Pages that maintain their own rects re-publish them.
                try { t.setInteractiveRects && t.setInteractiveRects([{ x: 0, y: 0, w: 99999, h: 99999 }]); } catch (e) {}
                // Strip every focused marker — mouse users get the existing
                // :hover styling, the focused-state CSS should never apply.
                for (const stale of document.querySelectorAll('[data-tsic-focused]')) {
                    stale.removeAttribute('data-tsic-focused');
                }
            }
        });

        let lastNavAt = 0;
        t.on('tsic.msg.UI.Input.IA_UI_Navigate', (payload) => {
            if (!State.enabled || State.mode !== 'Gamepad') return;
            const phase = payload && payload.Phase;
            if (phase !== 'Started' && phase !== 'Triggered') return;
            const v = (payload && payload.Value) || { X: 0, Y: 0 };
            const ax = Math.abs(v.X), ay = Math.abs(v.Y);
            if (ax < 0.4 && ay < 0.4) return;
            const now = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
            if (phase === 'Triggered' && (now - lastNavAt) < 180) return;
            lastNavAt = now;
            const dir = (ax > ay)
                ? (v.X > 0 ? 'right' : 'left')
                : (v.Y > 0 ? 'up' : 'down');
            api.step(dir);
        });

        t.on('tsic.msg.UI.Input.IA_UI_ConfirmAccept', (payload) => {
            if (!State.enabled || State.mode !== 'Gamepad') return;
            if (!payload || payload.Phase !== 'Started') return;
            const a = document.activeElement;
            if (a && typeof a.click === 'function') {
                try { a.click(); } catch (e) { console.warn('[tsic-focus] confirm click failed', e); }
            }
        });

        t.on('tsic.msg.UI.Input.IA_UI_CancelBack', (payload) => {
            if (!State.enabled || State.mode !== 'Gamepad') return;
            if (!payload || payload.Phase !== 'Started') return;
            if (State.scopeStack.length > 0) {
                api.popScope();
            }
            // else: let the page's own Esc / close-screen handler take over.
        });

        // First-paint stamp so CSS can branch immediately.
        stampMode(State.mode);

        // ---- :hover → [data-tsic-focused] mirror ------------------------
        // CSS :hover is a UA-controlled state; we can't toggle it from JS.
        // To make focused elements look hovered without authors having to
        // duplicate every :hover rule, walk the stylesheets once and
        // synthesise a matching [data-tsic-focused] rule for every :hover
        // rule, gated to html[data-tsic-input="Gamepad"]. Inserted as a
        // single <style id="tsic-focus-hover-mirror"> at the end of <head>
        // so it wins on equal specificity.
        function mirrorHoverRules() {
            if (document.getElementById('tsic-focus-hover-mirror')) return;
            const mirrors = [];
            for (const sheet of Array.from(document.styleSheets)) {
                let rules = null;
                try { rules = sheet.cssRules; } catch (e) { continue; /* cross-origin */ }
                if (!rules) continue;
                walkRules(rules, mirrors);
            }
            if (mirrors.length === 0) return;
            const style = document.createElement('style');
            style.id = 'tsic-focus-hover-mirror';
            style.textContent = mirrors.join('\n');
            document.head.appendChild(style);
        }
        function walkRules(rules, out) {
            for (const rule of Array.from(rules)) {
                // Style rule — the common case.
                if (rule.type === 1 /* CSSRule.STYLE_RULE */) {
                    const mirrored = mirrorSelector(rule.selectorText || '');
                    if (mirrored && rule.style && rule.style.cssText) {
                        out.push(mirrored + ' { ' + rule.style.cssText + ' }');
                    }
                    continue;
                }
                // @media / @supports — recurse and re-wrap.
                if (rule.cssRules && (rule.type === 4 || rule.type === 12)) {
                    const inner = [];
                    walkRules(rule.cssRules, inner);
                    if (inner.length === 0) continue;
                    const condition = (rule.media && rule.media.mediaText)
                        || (rule.conditionText || '');
                    const at = (rule.type === 4) ? '@media' : '@supports';
                    out.push(at + ' ' + condition + ' {\n' + inner.join('\n') + '\n}');
                }
            }
        }
        function mirrorSelector(selectorText) {
            if (!selectorText || selectorText.indexOf(':hover') === -1) return null;
            const parts = selectorText.split(',').map(s => s.trim()).filter(Boolean);
            const mapped = [];
            for (const p of parts) {
                if (p.indexOf(':hover') === -1) continue;
                // Replace :hover with our focused attribute. Multiple :hover
                // in one selector (rare, e.g. .a:hover .b:hover) — replace all.
                const focused = p.replace(/:hover/g, '[data-tsic-focused]');
                mapped.push('html[data-tsic-input="Gamepad"] ' + focused);
            }
            return mapped.length ? mapped.join(', ') : null;
        }
        // Run after the page's CSS has loaded. <link rel="stylesheet"> blocks
        // before DOMContentLoaded, but @import inside an existing sheet may
        // resolve a tick later — give it one rAF.
        function scheduleMirror() {
            if (typeof requestAnimationFrame === 'function') {
                requestAnimationFrame(() => requestAnimationFrame(mirrorHoverRules));
            } else {
                setTimeout(mirrorHoverRules, 32);
            }
        }
        if (document.readyState === 'complete') scheduleMirror();
        else window.addEventListener('load', scheduleMirror, { once: true });

        if (metaSaysEnabled()) {
            // Defer to next tick so the page's render pass can populate
            // [data-tsic-initial-focus] elements before we look for them.
            setTimeout(() => api.enable(), 0);
        }
    }

    (function poll() {
        if (window.tsic && typeof window.tsic.on === 'function') { install(window.tsic); return; }
        setTimeout(poll, 16);
    })();
})();

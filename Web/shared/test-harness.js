// TSIC SPA test harness.
//
// Provides a fake window.tsic for an iframe-loaded screen so scenarios can
// inject messages and observe what the page publishes back.
//
// Lives in two halves:
//   1. The HOST page (screens/tests.html) loads this file, then for each
//      scenario creates an <iframe> for the target screen. Before the
//      iframe's own scripts run it injects a mock tsic via
//      installMockTsic(iframeWindow). The mock matches the production
//      window.tsic API surface used by all SPA pages.
//   2. Each scenario file under /tests/<name>.js registers itself with
//      TSICTestHarness.scenarios.push({name, file, run}). run(ctx) gets a
//      context object with helpers (inject, expectPublish, expectEl, …) and
//      should return / resolve when done.
//
// The harness is intentionally framework-free — no Jest / Mocha — to keep it
// runnable inside Ultralight without a build step.

(function (global) {
    const NS = global.TSICTestHarness = global.TSICTestHarness || {
        scenarios: [],
        register(scenario) { NS.scenarios.push(scenario); },
    };

    NS.installMockTsic = function (win, options) {
        options = options || {};
        const subscribers = new Map();   // channel -> Set(callbacks)
        const stickyCache = new Map();   // channel -> last payload
        const publishLog  = [];           // {channel, payload, t}

        const fake = {
            on(channel, cb) {
                if (!subscribers.has(channel)) subscribers.set(channel, new Set());
                subscribers.get(channel).add(cb);
                // Replay sticky cache, matching the production bridge.
                if (stickyCache.has(channel)) {
                    try { cb(stickyCache.get(channel)); } catch (e) { console.warn('[harness] cb threw', e); }
                }
                return () => subscribers.get(channel).delete(cb);
            },
            publishMessage(channel, payload) {
                publishLog.push({ channel, payload, t: Date.now() });
                if (subscribers.has(channel)) {
                    for (const cb of subscribers.get(channel)) {
                        try { cb(payload); } catch (e) { console.warn('[harness] cb threw', e); }
                    }
                }
            },
            // No-op modder helpers — pages call them but tests don't need them.
            appendInputModeTag() {},
            removeInputModeTag() {},
            setMenuActionContext() {},
            clearMenuActionContext() {},
            itemCatalog: options.itemCatalog || {},
            recipeCatalog: options.recipeCatalog || {},
            itemName(id) { const d = this.itemCatalog[id]; return d ? (d.Name || id) : id; },
            itemCategory(id) { const d = this.itemCatalog[id]; return d ? d.Category : null; },
            itemIconUrl(id) { return `tex://item-icon/${encodeURIComponent(id)}`; },
        };

        const handle = {
            // Inject a C++ -> JS message into the iframe.
            inject(channel, payload) {
                stickyCache.set(channel, payload);
                if (subscribers.has(channel)) {
                    for (const cb of subscribers.get(channel)) {
                        try { cb(payload); } catch (e) { console.warn('[harness] cb threw', e); }
                    }
                }
            },
            // Fire an Enhanced-Input event as if the C++ bridge published it.
            input(actionName, phase, value) {
                const channel = `tsic.msg.UI.Input.${actionName}`;
                const payload = {
                    Action: actionName,
                    Phase: phase || 'Triggered',
                    Value: value || { X: 0, Y: 0, Z: 0 },
                    ElapsedSec: 1 / 60,
                    TriggeredSec: 0,
                };
                handle.inject(channel, payload);
            },
            // Simulate UI.Screen.Changed (drives router + visibility gates).
            screen(name) { handle.inject('tsic.msg.UI.Screen.Changed', { Name: name }); },
            // Simulate UI.Input.Mode.Changed.
            mode(device) { handle.inject('tsic.msg.UI.Input.Mode.Changed', { Mode: device, Device: device.toLowerCase() }); },
            // Mutate the catalog and fire the change event the SPA listens for.
            setItemCatalog(map) {
                fake.itemCatalog = map || {};
                try { win.dispatchEvent(new Event('tsic-item-catalog')); } catch (e) {}
            },
            setRecipeCatalog(map) {
                fake.recipeCatalog = map || {};
                try { win.dispatchEvent(new Event('tsic-recipe-catalog')); } catch (e) {}
            },
            // Inspection: snapshot + clear of the publish log.
            publishes() { return publishLog.slice(); },
            clearPublishes() { publishLog.length = 0; },
            // Subscriber introspection (used by tests to assert "page subscribed at all").
            channels() { return Array.from(subscribers.keys()); },
        };

        // Install on the iframe's window so the page's `if (window.tsic)` checks pass.
        win.tsic = fake;
        // Expose the handle to the host for scenario authoring.
        win.__tsicTestHandle = handle;
        return handle;
    };

    // Assertion helpers — return strings on failure, null on pass.
    NS.assert = {
        eq(actual, expected, label) {
            const ok = JSON.stringify(actual) === JSON.stringify(expected);
            return ok ? null : `${label || 'expect.eq'}: got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)}`;
        },
        truthy(value, label) {
            return value ? null : `${label || 'expect.truthy'}: got ${JSON.stringify(value)}`;
        },
        published(handle, channel, opts) {
            const matches = handle.publishes().filter(p => p.channel === channel);
            if (matches.length === 0) return `expected publish on '${channel}', got none. Channels published: ${JSON.stringify(handle.publishes().map(p => p.channel))}`;
            if (opts && opts.where) {
                const ok = matches.some(m => opts.where(m.payload));
                if (!ok) return `expected publish on '${channel}' matching predicate; payloads were ${JSON.stringify(matches.map(m => m.payload))}`;
            }
            return null;
        },
        notPublished(handle, channel) {
            const matches = handle.publishes().filter(p => p.channel === channel);
            return matches.length === 0 ? null : `expected NO publish on '${channel}', got ${matches.length}`;
        },
        domText(doc, selector, expected) {
            const el = doc.querySelector(selector);
            if (!el) return `expected '${selector}' to exist`;
            const got = (el.textContent || '').trim();
            if (typeof expected === 'string') {
                return got === expected ? null : `expected '${selector}' text '${expected}', got '${got}'`;
            }
            if (expected instanceof RegExp) {
                return expected.test(got) ? null : `expected '${selector}' text to match ${expected}, got '${got}'`;
            }
            return `unknown expected type for domText`;
        },
        domCount(doc, selector, n) {
            const c = doc.querySelectorAll(selector).length;
            return c === n ? null : `expected ${n} of '${selector}', got ${c}`;
        },
        domExists(doc, selector) {
            return doc.querySelector(selector) ? null : `expected '${selector}' to exist`;
        },
        domHidden(doc, selector) {
            const el = doc.querySelector(selector);
            if (!el) return `expected '${selector}' to exist (so we can assert hidden)`;
            const cs = doc.defaultView.getComputedStyle(el);
            const hidden = (cs.display === 'none' || cs.visibility === 'hidden');
            return hidden ? null : `expected '${selector}' to be hidden, but display=${cs.display} visibility=${cs.visibility}`;
        },
        domVisible(doc, selector) {
            const el = doc.querySelector(selector);
            if (!el) return `expected '${selector}' to exist (so we can assert visible)`;
            const cs = doc.defaultView.getComputedStyle(el);
            const hidden = (cs.display === 'none' || cs.visibility === 'hidden');
            return !hidden ? null : `expected '${selector}' to be visible`;
        },
    };

    // Synthetic DOM event helpers for input simulation inside an iframe.
    // KeyboardEvent / MouseEvent must come from the target document's window
    // so the event passes constructor-instanceof checks inside the iframe.
    function W(doc) { return (doc && doc.defaultView) || global; }
    NS.events = {
        click(doc, selector) {
            const el = doc.querySelector(selector);
            if (!el) throw new Error(`click: no element '${selector}'`);
            el.click();
        },
        key(doc, key, opts) {
            const init = Object.assign({ key, code: opts && opts.code || `Key${(key || '').toUpperCase()}`, bubbles: true, cancelable: true }, opts || {});
            const w = W(doc);
            doc.dispatchEvent(new w.KeyboardEvent('keydown', init));
            doc.dispatchEvent(new w.KeyboardEvent('keyup', init));
        },
        keyOn(el, key, opts) {
            const init = Object.assign({ key, code: opts && opts.code || `Key${(key || '').toUpperCase()}`, bubbles: true, cancelable: true }, opts || {});
            const w = W(el.ownerDocument);
            el.dispatchEvent(new w.KeyboardEvent('keydown', init));
            el.dispatchEvent(new w.KeyboardEvent('keyup', init));
        },
        mouse(doc, selector, type, opts) {
            const el = doc.querySelector(selector);
            if (!el) throw new Error(`mouse: no element '${selector}'`);
            const w = W(doc);
            el.dispatchEvent(new w.MouseEvent(type || 'click', Object.assign({ bubbles: true, cancelable: true, button: 0 }, opts || {})));
        },
    };

    // Focus-engine helpers. Returned as a sub-object that tests.html mounts
    // onto the per-scenario ctx via `ctx.focus = TSICTestHarness.makeFocusHelpers(...)`.
    // Lives here (rather than inline in tests.html) so it can be unit-tested
    // and to keep the iframe ctx builder small.
    NS.makeFocusHelpers = function (iframe, handle, failures) {
        function doc()    { return iframe.contentDocument; }
        function win()    { return iframe.contentWindow; }
        function engine() { return (win() && win().tsic && win().tsic.focus) || null; }
        function active() { const d = doc(); return d ? d.activeElement : null; }
        function delay(ms) { return new Promise(r => setTimeout(r, ms)); }

        const fx = {
            pressDir(dir) {
                const map = { up: { X: 0, Y: 1 }, down: { X: 0, Y: -1 }, left: { X: -1, Y: 0 }, right: { X: 1, Y: 0 } };
                const v = map[dir] || { X: 0, Y: 0 };
                handle.input('IA_UI_Navigate', 'Started', { X: v.X, Y: v.Y, Z: 0 });
            },
            confirm() { handle.input('IA_UI_ConfirmAccept', 'Started'); },
            cancel()  { handle.input('IA_UI_CancelBack', 'Started'); },
            active,
            activeId() {
                const el = active();
                if (!el) return null;
                if (el.id) return '#' + el.id;
                if (el.dataset && el.dataset.tsicFocusId) {
                    return '[data-tsic-focus-id="' + el.dataset.tsicFocusId + '"]';
                }
                const path = [];
                let n = el;
                const root = doc() && doc().body;
                while (n && n.nodeType === 1 && n !== root) {
                    const sib = Array.from(n.parentNode.children).filter(c => c.tagName === n.tagName);
                    const idx = sib.indexOf(n) + 1;
                    path.unshift(n.tagName.toLowerCase() + ':nth-of-type(' + idx + ')');
                    n = n.parentNode;
                }
                return path.join(' > ');
            },
            resetMemory() {
                const e = engine();
                if (e && e.resetMemory) e.resetMemory();
            },
            disableSmoothScroll() {
                const e = engine();
                if (e && e.__state) e.__state.smoothScroll = false;
            },
            focus(elOrSel) {
                const e = engine();
                if (e && e.focus) e.focus(elOrSel);
            },

            // Reachability A: BFS from initial focus must visit every focusable.
            async assertAllReachable(opts) {
                const e = engine();
                if (!e) { failures.push('assertAllReachable: no engine'); return; }
                const filter = (opts && opts.filter) || null;
                // Prefer the rect-filtered set; in layout-less environments
                // (jsdom) fall back to the structural set.
                let focusables = e.__focusableSet ? e.__focusableSet() : [];
                if (focusables.length === 0 && e.__structuralFocusableSet) {
                    focusables = e.__structuralFocusableSet();
                }
                if (filter) focusables = focusables.filter(filter);
                if (focusables.length === 0) {
                    failures.push('assertAllReachable: no focusable elements found');
                    return;
                }
                const start = active();
                if (!start || !focusables.includes(start)) {
                    failures.push('assertAllReachable: no initial focus or active is not focusable (active=' + (start && start.tagName) + ')');
                    return;
                }
                const visited = new Set([start]);
                const queue = [start];
                const dirs = ['up', 'down', 'left', 'right'];
                let budget = focusables.length * 8;
                while (queue.length && budget-- > 0) {
                    const cur = queue.shift();
                    for (const d of dirs) {
                        // Re-focus the current node so each direction starts
                        // from the same anchor (each pressDir mutates focus).
                        e.focus(cur, { trust: true });
                        await delay(4);
                        fx.pressDir(d);
                        await delay(8);
                        const next = active();
                        if (next && next !== cur && !visited.has(next) && focusables.includes(next)) {
                            visited.add(next);
                            queue.push(next);
                        }
                    }
                }
                const missing = focusables.filter(f => !visited.has(f));
                if (missing.length > 0) {
                    const desc = missing.slice(0, 6).map(el => {
                        if (el.id) return '#' + el.id;
                        const cls = (el.className && el.className.toString) ? el.className.toString().split(' ').slice(0, 2).join('.') : '';
                        return el.tagName.toLowerCase() + (cls ? '.' + cls : '');
                    }).join(', ');
                    failures.push('assertAllReachable: ' + missing.length + ' unreachable: ' + desc + (missing.length > 6 ? ' ...' : ''));
                }
            },

            // Reachability B: every focus-group reachable from every other.
            async assertAllGroupsMutuallyReachable() {
                const e = engine();
                if (!e) { failures.push('assertAllGroupsMutuallyReachable: no engine'); return; }
                let focusables = e.__focusableSet ? e.__focusableSet() : [];
                if (focusables.length === 0 && e.__structuralFocusableSet) {
                    focusables = e.__structuralFocusableSet();
                }
                const groupOf = (el) => {
                    const g = el.closest('[data-tsic-focus-group]');
                    return g ? g.getAttribute('data-tsic-focus-group') : null;
                };
                const groups = new Set(focusables.map(groupOf).filter(Boolean));
                if (groups.size < 2) return;
                const edges = new Map();
                for (const g of groups) edges.set(g, new Set());
                const visited = new Set();
                const queue = [];
                const start = active();
                if (start) { visited.add(start); queue.push(start); }
                let budget = focusables.length * 8;
                while (queue.length && budget-- > 0) {
                    const cur = queue.shift();
                    const curGroup = groupOf(cur);
                    for (const d of ['up','down','left','right']) {
                        e.focus(cur, { trust: true });
                        await delay(4);
                        fx.pressDir(d);
                        await delay(8);
                        const next = active();
                        if (!next || next === cur) continue;
                        const nextGroup = groupOf(next);
                        if (curGroup && nextGroup && curGroup !== nextGroup) {
                            edges.get(curGroup).add(nextGroup);
                        }
                        if (!visited.has(next) && focusables.includes(next)) {
                            visited.add(next);
                            queue.push(next);
                        }
                    }
                }
                const reachable = (from) => {
                    const seen = new Set([from]);
                    const q = [from];
                    while (q.length) {
                        const c = q.shift();
                        for (const n of edges.get(c) || []) {
                            if (!seen.has(n)) { seen.add(n); q.push(n); }
                        }
                    }
                    return seen;
                };
                const list = Array.from(groups);
                const missing = [];
                for (const a of list) {
                    const r = reachable(a);
                    for (const b of list) {
                        if (a !== b && !r.has(b)) missing.push(a + ' -> ' + b);
                    }
                }
                if (missing.length > 0) {
                    failures.push('assertAllGroupsMutuallyReachable: ' + missing.length + ' disconnected pair(s): ' + missing.slice(0,6).join('; ') + (missing.length > 6 ? ' ...' : ''));
                }
            },

            // Open every .tsic-dropdown trigger, walk options, cancel, assert focus returns.
            async assertDropdownsRoundtrip() {
                const d = doc();
                const triggers = Array.from(d.querySelectorAll('.tsic-dropdown'))
                    .filter(b => !b.closest('.tsic-dropdown-portal'));
                for (const trigger of triggers) {
                    fx.focus(trigger);
                    await delay(16);
                    fx.confirm();
                    await delay(40);
                    const portal = d.querySelector('.tsic-dropdown-portal [role="listbox"]');
                    if (!portal) {
                        failures.push('assertDropdownsRoundtrip: confirm on ' + (trigger.id || trigger.className) + ' did not open a portal');
                        continue;
                    }
                    fx.pressDir('down'); await delay(16);
                    fx.cancel(); await delay(40);
                    if (d.querySelector('.tsic-dropdown-portal')) {
                        failures.push('assertDropdownsRoundtrip: cancel on ' + (trigger.id || trigger.className) + ' did not close portal');
                    }
                    if (active() !== trigger) {
                        failures.push('assertDropdownsRoundtrip: focus did not return to ' + (trigger.id || trigger.className) + ' after cancel');
                    }
                }
            },
        };
        return fx;
    };

    // Focus test fixtures. Lifted to the harness so they're available in both
    // jsdom (file-scope closure) and playwright (page.evaluate of just
    // scn.run.toString() — file-scope helpers are otherwise out of reach).
    NS.fx = {
        // Add a tsic-focus opt-in + replace body content. Idempotent — only
        // inserts the meta if it isn't already there.
        setupFixture(ctx, bodyHTML) {
            if (!ctx.doc.querySelector('meta[name="tsic-focus"]')) {
                ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
            }
            ctx.doc.body.innerHTML = bodyHTML;
        },
        // Pin a deterministic rect onto an element. Used to make spatial-nearest
        // math testable in jsdom (which doesn't compute layout). No-op-safe in
        // browsers — overriding getBoundingClientRect on an instance still works.
        mockRect(el, x, y, w, h) {
            if (!el) return;
            const r = { left: x, top: y, width: w, height: h, right: x + w, bottom: y + h, x: x, y: y };
            el.getBoundingClientRect = () => r;
        },
        // Auto-apply rects to a list of elements based on their inline
        // style.{left,top,width,height}. In real-layout environments callers
        // should pass { onlyIfZeroRect: true } so we don't trample real rects.
        applyRects(elements, opts) {
            const o = opts || {};
            const w0 = o.defaultW || 100;
            const h0 = o.defaultH || 28;
            for (const el of elements) {
                if (o.onlyIfZeroRect) {
                    const cur = el.getBoundingClientRect();
                    if (cur && cur.width > 0 && cur.height > 0) continue;
                }
                const sx = parseFloat(el.style.left || '0') || 0;
                const sy = parseFloat(el.style.top  || '0') || 0;
                const sw = parseFloat(el.style.width  || '') || w0;
                const sh = parseFloat(el.style.height || '') || h0;
                NS.fx.mockRect(el, sx, sy, sw, sh);
            }
        },
        // Wait up to timeoutMs (default 2000) for the engine to land focus on
        // [data-tsic-initial-focus]. Returns the element or null.
        async awaitInitialFocus(ctx, timeoutMs) {
            const start = Date.now();
            const limit = timeoutMs || 2000;
            while (Date.now() - start < limit) {
                const a = ctx.doc.activeElement;
                if (a && a !== ctx.doc.body && a.matches && a.matches('[data-tsic-initial-focus]')) return a;
                await new Promise(r => setTimeout(r, 16));
            }
            return null;
        },
        // Run the three reachability asserts on the page as it stands.
        async runReachability(ctx) {
            ctx.focus.disableSmoothScroll();
            ctx.focus.resetMemory();
            ctx.mode('Gamepad');
            const got = await NS.fx.awaitInitialFocus(ctx);
            if (!got) {
                const a = ctx.doc.activeElement;
                ctx.expect('initial focus never landed on [data-tsic-initial-focus]; activeElement=' +
                    (a ? a.tagName + (a.id ? '#'+a.id : '') : 'null'));
                return;
            }
            await ctx.focus.assertAllReachable();
            await ctx.focus.assertAllGroupsMutuallyReachable();
            await ctx.focus.assertDropdownsRoundtrip();
        },
    };

    // Tiny "wait until predicate or timeout" helper for async DOM updates.
    NS.waitFor = function (predicate, opts) {
        const timeout = (opts && opts.timeout) || 1000;
        const interval = (opts && opts.interval) || 16;
        const start = Date.now();
        return new Promise((resolve, reject) => {
            const tick = () => {
                let v;
                try { v = predicate(); } catch (e) { v = false; }
                if (v) { resolve(v); return; }
                if (Date.now() - start > timeout) { reject(new Error('waitFor timeout')); return; }
                setTimeout(tick, interval);
            };
            tick();
        });
    };
})(window);

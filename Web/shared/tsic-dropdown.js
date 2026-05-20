// shared/tsic-dropdown.js
//
// Focus-aware dropdown that replaces native <select> on pages we need to make
// gamepad-navigable. The trigger button is a normal focusable element; on
// confirm/click it opens a popup <ul role="listbox"> in a body-mounted portal
// and pushes a focus scope. Up/Down moves between options, confirm picks,
// cancel/outside-click closes without changing.
//
// Markup:
//   <button class="tsic-dropdown" id="my-dd" data-tsic-focusable
//           data-tsic-options='[{"value":"a","label":"A"}, ...]'
//           data-tsic-value="a">
//     <span class="tsic-dropdown-label">A</span>
//     <span class="tsic-dropdown-caret">▾</span>
//   </button>
//
// API (window.tsic.dropdown):
//   .get(triggerOrSel)                      -> currentValue or null
//   .set(triggerOrSel, value)               -> updates label, fires 'tsic-change'
//   .options(triggerOrSel, [{value,label}]) -> repopulate (preserves selection if still valid)
//   .open(triggerOrSel) / .close()
(function () {
    function install(t) {
        if (t.__dropdownInstalled) return;
        t.__dropdownInstalled = true;

        function resolve(target) {
            return (typeof target === 'string') ? document.querySelector(target) : target;
        }

        function parseOptions(trigger) {
            const raw = trigger.getAttribute('data-tsic-options');
            if (!raw) return [];
            try {
                const parsed = JSON.parse(raw);
                return Array.isArray(parsed) ? parsed : [];
            } catch (e) {
                console.warn('[tsic-dropdown] bad data-tsic-options on', trigger, e);
                return [];
            }
        }

        function labelEl(trigger) {
            return trigger.querySelector('.tsic-dropdown-label') || trigger;
        }

        const Open = { trigger: null, portal: null, onDocClick: null, cleaning: false };

        // Tear down DOM + listeners. Pure cleanup — does NOT call popScope.
        // Used as the engine's onPop callback when CancelBack pops our scope,
        // and as the body of close() after we explicitly pop the scope.
        function teardown() {
            if (Open.cleaning) return;
            Open.cleaning = true;
            const trigger = Open.trigger;
            const portal = Open.portal;
            const onDocClick = Open.onDocClick;
            Open.trigger = null; Open.portal = null; Open.onDocClick = null;
            if (trigger) { try { trigger.setAttribute('aria-expanded', 'false'); } catch (e) {} }
            if (portal && portal.parentNode) portal.parentNode.removeChild(portal);
            if (onDocClick) document.removeEventListener('mousedown', onDocClick, true);
            Open.cleaning = false;
        }

        // Close initiated by code (commit / outside-click / explicit close()).
        // Pops the scope first; popScope runs our onPop which calls teardown.
        function close() {
            if (!Open.trigger) return;
            if (t.focus && t.focus.popScope) {
                t.focus.popScope();
            } else {
                teardown();
            }
        }

        function commit(trigger, value) {
            t.dropdown.set(trigger, value);
            close();
        }

        function open(trigger) {
            if (Open.trigger) close();
            const options = parseOptions(trigger);
            if (options.length === 0) return;

            const portal = document.createElement('div');
            portal.className = 'tsic-dropdown-portal';
            const list = document.createElement('ul');
            list.setAttribute('role', 'listbox');

            const currentValue = trigger.getAttribute('data-tsic-value');
            let initial = null;
            for (const opt of options) {
                const li = document.createElement('li');
                li.setAttribute('role', 'option');
                li.setAttribute('data-tsic-focusable', '');
                li.setAttribute('tabindex', '-1');
                li.dataset.value = String(opt.value);
                li.textContent = String(opt.label != null ? opt.label : opt.value);
                if (currentValue != null && String(opt.value) === currentValue) {
                    li.setAttribute('aria-selected', 'true');
                    initial = li;
                }
                li.addEventListener('click', (ev) => {
                    ev.stopPropagation();
                    commit(trigger, String(opt.value));
                });
                list.appendChild(li);
            }
            portal.appendChild(list);
            document.body.appendChild(portal);

            const r = trigger.getBoundingClientRect();
            portal.style.left = Math.round(r.left) + 'px';
            portal.style.minWidth = Math.round(r.width) + 'px';
            const portalH = portal.getBoundingClientRect().height;
            const below = (r.bottom + portalH <= window.innerHeight);
            portal.style.top = (below ? Math.round(r.bottom) : Math.round(r.top - portalH)) + 'px';

            try { trigger.setAttribute('aria-expanded', 'true'); } catch (e) {}

            const onDocClick = (ev) => {
                if (!portal.contains(ev.target) && !trigger.contains(ev.target)) close();
            };
            document.addEventListener('mousedown', onDocClick, true);

            Open.trigger = trigger; Open.portal = portal; Open.onDocClick = onDocClick;

            if (t.focus && t.focus.pushScope) {
                t.focus.pushScope(portal, initial || list.firstElementChild, { onPop: teardown });
            } else if (initial || list.firstElementChild) {
                try { (initial || list.firstElementChild).focus(); } catch (e) {}
            }
        }

        t.dropdown = {
            get(triggerOrSel) {
                const tg = resolve(triggerOrSel); if (!tg) return null;
                const v = tg.getAttribute('data-tsic-value');
                return v != null ? v : null;
            },
            set(triggerOrSel, value) {
                const tg = resolve(triggerOrSel); if (!tg) return;
                tg.setAttribute('data-tsic-value', String(value));
                const opts = parseOptions(tg);
                const match = opts.find(o => String(o.value) === String(value));
                if (match) {
                    labelEl(tg).textContent = String(match.label != null ? match.label : match.value);
                }
                tg.dispatchEvent(new CustomEvent('tsic-change', { detail: { value: String(value) }, bubbles: true }));
            },
            options(triggerOrSel, opts) {
                const tg = resolve(triggerOrSel); if (!tg) return;
                tg.setAttribute('data-tsic-options', JSON.stringify(opts || []));
                const current = tg.getAttribute('data-tsic-value');
                if (current) {
                    const still = (opts || []).find(o => String(o.value) === current);
                    if (!still) {
                        tg.removeAttribute('data-tsic-value');
                        labelEl(tg).textContent = '';
                    } else {
                        labelEl(tg).textContent = String(still.label != null ? still.label : still.value);
                    }
                }
            },
            open(triggerOrSel) { const tg = resolve(triggerOrSel); if (tg) open(tg); },
            close() { close(); },
        };

        // Auto-wire: clicking a .tsic-dropdown trigger opens it. Clicks inside an
        // open portal are handled by the per-option click listener.
        document.addEventListener('click', (ev) => {
            const trig = ev.target && ev.target.closest && ev.target.closest('.tsic-dropdown');
            if (!trig) return;
            if (trig.closest('.tsic-dropdown-portal')) return;
            ev.preventDefault();
            if (Open.trigger === trig) { close(); return; }
            open(trig);
        });
    }

    (function poll() {
        if (window.tsic && typeof window.tsic.on === 'function') { install(window.tsic); return; }
        setTimeout(poll, 16);
    })();
})();

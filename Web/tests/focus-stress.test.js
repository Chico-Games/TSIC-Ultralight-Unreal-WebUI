// Stress / edge-case tests for the focus engine.
// Designed to break weirdly: rapid mode flipping, partial layout, modal-in-
// modal, dead-zone, diagonals, dynamic disable/enable, etc.

TSICTestHarness.register({
    name: 'Focus/Stress: Mode flip rapidly does not lose focus memory',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" data-tsic-initial-focus style="position:absolute; left:0;   top:0;">a</button>' +
              '<button id="b"                          style="position:absolute; left:0;   top:50px;">b</button>' +
              '<button id="c"                          style="position:absolute; left:0;   top:100px;">c</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 30));
        ctx.win.tsic.focus.focus('#c');
        // Flip mode 6 times — memory should survive.
        for (let i = 0; i < 6; i++) {
            ctx.mode(i % 2 === 0 ? 'MouseAndKeyboard' : 'Gamepad');
            await new Promise(r => setTimeout(r, 15));
        }
        // End on Gamepad — engine should restore #c.
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 40));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'c'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: deadzone — small stick values do not move focus',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" data-tsic-initial-focus style="position:absolute; left:0;   top:0;">a</button>' +
              '<button id="b"                          style="position:absolute; left:0;   top:60px;">b</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        // Below the engine's 0.4 deadzone — should be ignored.
        ctx.input('IA_UI_Navigate', 'Started', { X: 0.2, Y: 0.2, Z: 0 });
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'a'));
        // Above the deadzone — should move.
        ctx.input('IA_UI_Navigate', 'Started', { X: 0, Y: -1, Z: 0 });
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: diagonal stick — dominant axis wins',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="cur" data-tsic-initial-focus style="position:absolute; left:200px; top:200px;">cur</button>' +
              '<button id="right" style="position:absolute; left:400px; top:200px;">right</button>' +
              '<button id="down"  style="position:absolute; left:200px; top:400px;">down</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        // X=0.9, Y=-0.4 — X dominates, so we should go right.
        ctx.input('IA_UI_Navigate', 'Started', { X: 0.9, Y: -0.4, Z: 0 });
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'right'));
        // Now Y dominates — should go down (from current 'right' button).
        ctx.win.tsic.focus.focus('#cur');
        await new Promise(r => setTimeout(r, 20));
        ctx.input('IA_UI_Navigate', 'Started', { X: 0.4, Y: -0.9, Z: 0 });
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'down'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: disabled element mid-list is skipped on nav',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" data-tsic-initial-focus style="position:absolute; left:0; top:0;">a</button>' +
              '<button id="b" disabled                  style="position:absolute; left:0; top:50px;">b</button>' +
              '<button id="c"                           style="position:absolute; left:0; top:100px;">c</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.focus.pressDir('down');
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'c'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: element that becomes disabled mid-nav resets to initial',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" data-tsic-initial-focus style="position:absolute; left:0; top:0;">a</button>' +
              '<button id="b" style="position:absolute; left:0; top:50px;">b</button>' +
              '<button id="c" style="position:absolute; left:0; top:100px;">c</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.focus.pressDir('down');
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b'));
        // Disable the focused button — focus is now on something the engine
        // no longer considers focusable. The next direction press should
        // recover by snapping to the canonical initial-focus rather than
        // getting stuck on the disabled element.
        ctx.doc.getElementById('b').disabled = true;
        ctx.focus.pressDir('down');
        await new Promise(r => setTimeout(r, 20));
        // Either we snapped to initial 'a' (safe recovery) or to 'c' (engine
        // walks past the disabled b spatially). Both are acceptable; the
        // *unacceptable* outcome is staying on the disabled 'b'.
        const cur = ctx.doc.activeElement.id;
        ctx.expect(ctx.assert.truthy(cur === 'a' || cur === 'c',
            'after disabling focused element, expected recovery to a or c; got ' + cur));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: hidden tab content is not navigable',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="tabs" style="position:fixed; inset:0;">' +
              '<button id="tab-A" data-tsic-initial-focus style="position:absolute; left:0; top:0;">A</button>' +
              '<button id="tab-B"                          style="position:absolute; left:60px; top:0;">B</button>' +
            '</div>' +
            '<div data-tsic-focus-group="content" style="position:fixed; top:100px; left:0; right:0; bottom:0;">' +
              '<div id="paneA"><button id="a1" style="position:absolute; left:0; top:100px;">a1</button></div>' +
              '<div id="paneB" style="display:none;"><button id="b1" style="position:absolute; left:0; top:100px;">b1</button></div>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        // Zero out the hidden pane's button explicitly so the rect filter excludes it.
        TSICTestHarness.fx.mockRect(ctx.doc.getElementById('b1'), 0, 0, 0, 0);
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        const set = ctx.win.tsic.focus.__focusableSet().map(el => el.id);
        ctx.expect(ctx.assert.truthy(set.includes('a1'),  'a1 should be focusable'));
        ctx.expect(ctx.assert.truthy(!set.includes('b1'), 'b1 in display:none parent should NOT be focusable'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: scope-in-scope (modal opens nested modal)',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="outer" style="position:fixed; inset:0;">' +
              '<button id="o" data-tsic-initial-focus style="position:absolute; left:0; top:0;">o</button>' +
              '<div id="m1" data-tsic-focus-group="m1" style="position:absolute; left:200px; top:200px;">' +
                '<button id="m1a" style="position:absolute; left:200px; top:200px;">m1a</button>' +
                '<div id="m2" data-tsic-focus-group="m2" style="position:absolute; left:400px; top:400px;">' +
                  '<button id="m2a" style="position:absolute; left:400px; top:400px;">m2a</button>' +
                '</div>' +
              '</div>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.win.tsic.focus.pushScope(ctx.doc.getElementById('m1'), '#m1a');
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'm1a'));
        ctx.win.tsic.focus.pushScope(ctx.doc.getElementById('m2'), '#m2a');
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'm2a'));
        // Pop inner — should restore m1a.
        ctx.win.tsic.focus.popScope();
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'm1a'));
        // Pop outer — should restore o.
        ctx.win.tsic.focus.popScope();
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'o'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: dynamic re-render — refresh after replacing DOM',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div id="host" data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" data-tsic-initial-focus style="position:absolute; left:0; top:0;">a</button>' +
              '<button id="b"                          style="position:absolute; left:0; top:50px;">b</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        // Re-render the list with completely different elements, each given a
        // distinct vertical position so spatial nav has something to navigate.
        const host = ctx.doc.getElementById('host');
        host.innerHTML =
            '<button id="x" data-tsic-initial-focus style="position:absolute; left:0; top:0;">x</button>' +
            '<button id="y" style="position:absolute; left:0; top:50px;">y</button>' +
            '<button id="z" style="position:absolute; left:0; top:100px;">z</button>';
        TSICTestHarness.fx.applyRects(host.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.refresh();
        ctx.win.tsic.focus.focus('#x');
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'x'));
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'y'));
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'z'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: confirm on a list row fires its click handler',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="rows">' +
              '<div id="r1" data-tsic-focusable data-tsic-initial-focus tabindex="-1">row1</div>' +
              '<div id="r2" data-tsic-focusable                          tabindex="-1">row2</div>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('[data-tsic-focusable]'), { onlyIfZeroRect: true });
        let clicks = [];
        for (const el of ctx.doc.querySelectorAll('[data-tsic-focusable]')) {
            el.addEventListener('click', () => clicks.push(el.id));
        }
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(clicks, ['r1']));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: confirm in MouseAndKeyboard mode does nothing',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t"><button id="b" data-tsic-initial-focus>b</button></div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        let clicks = 0;
        ctx.doc.getElementById('b').addEventListener('click', () => clicks++);
        ctx.win.tsic.focus.enable();
        // Stay in MouseAndKeyboard mode.
        await new Promise(r => setTimeout(r, 30));
        ctx.focus.confirm(); // engine should ignore — not in Gamepad.
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(clicks, 0));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: pressDir in MouseAndKeyboard mode does not move focus',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" data-tsic-initial-focus style="position:absolute; left:0; top:0;">a</button>' +
              '<button id="b"                          style="position:absolute; left:0; top:50px;">b</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.mode('MouseAndKeyboard'); await new Promise(r => setTimeout(r, 30));
        const before = ctx.doc.activeElement && ctx.doc.activeElement.id;
        ctx.focus.pressDir('down');
        await new Promise(r => setTimeout(r, 20));
        const after = ctx.doc.activeElement && ctx.doc.activeElement.id;
        // Mouse mode — engine should be inert.
        ctx.expect(ctx.assert.eq(before, after));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: snapshot returns expected shape',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t"><button id="x" data-tsic-initial-focus>x</button><button id="y">y</button></div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        const snap = ctx.win.tsic.focus.snapshot();
        ctx.expect(ctx.assert.eq(snap.mode, 'Gamepad'));
        ctx.expect(ctx.assert.eq(snap.enabled, true));
        ctx.expect(ctx.assert.eq(snap.scope, 0));
        ctx.expect(ctx.assert.truthy(snap.focusable >= 2,
            'expected at least 2 focusable, got ' + snap.focusable));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: very long list — every row reachable via repeated down',
    file: '/screens/inventory.html',
    async run(ctx) {
        const N = 25;
        let html = '<div data-tsic-focus-group="rows" style="position:fixed; inset:0;">';
        for (let i = 0; i < N; i++) {
            const sel = (i === 0) ? ' data-tsic-initial-focus' : '';
            html += `<button id="r${i}" ${sel} style="position:absolute; left:0; top:${i * 30}px;">r${i}</button>`;
        }
        html += '</div>';
        TSICTestHarness.fx.setupFixture(ctx, html);
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 50));
        // Walk N-1 downs — should end at the last row.
        for (let i = 0; i < N - 1; i++) {
            ctx.focus.pressDir('down');
            await new Promise(r => setTimeout(r, 10));
        }
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'r' + (N - 1)));
        // Past the end, focus stays put.
        ctx.focus.pressDir('down');
        ctx.focus.pressDir('down');
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'r' + (N - 1)));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: empty page (no focusable) — engine does not throw',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
        ctx.doc.body.innerHTML = '<p>No interactive widgets here.</p>';
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        // No throw, no focus.
        ctx.expect(ctx.assert.truthy(true));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: Other / unknown mode names treated as MouseAndKeyboard',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t"><button id="b" data-tsic-initial-focus>b</button></div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.inject('tsic.msg.UI.Input.Mode.Changed', { Mode: 'Touch' });
        await new Promise(r => setTimeout(r, 30));
        // Engine treats anything-not-Gamepad as KB+M (inert) so confirm
        // shouldn't fire .click().
        let clicks = 0;
        ctx.doc.getElementById('b').addEventListener('click', () => clicks++);
        ctx.focus.confirm(); await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(clicks, 0));
        ctx.expect(ctx.assert.eq(ctx.doc.documentElement.getAttribute('data-tsic-input'), 'Touch'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: pushScope with no initial picks first focusable in root',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="caller" data-tsic-initial-focus style="position:absolute; left:0;   top:0;">caller</button>' +
              '<div id="popup" style="position:absolute; left:200px; top:200px;">' +
                '<button id="p1" style="position:absolute; left:200px; top:200px;">p1</button>' +
                '<button id="p2" style="position:absolute; left:200px; top:240px;">p2</button>' +
              '</div>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        // pushScope without an explicit initial — engine picks the first one.
        ctx.win.tsic.focus.pushScope(ctx.doc.getElementById('popup'));
        await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'p1'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: outside-click closes dropdown without committing',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
        ctx.doc.body.innerHTML =
            '<div data-tsic-focus-group="t" style="position:relative; padding:20px;">' +
              '<button id="dd" class="tsic-dropdown" data-tsic-focusable data-tsic-initial-focus ' +
                'data-tsic-options=\'[{"value":"a","label":"A"},{"value":"b","label":"B"}]\' ' +
                'data-tsic-value="a">A</button>' +
              '<div id="outside" style="height:200px; background: rgba(0,0,0,0.05); margin-top:50px;">outside area</div>' +
            '</div>';
        TSICTestHarness.fx.mockRect(ctx.doc.getElementById('dd'), 20, 20, 120, 28);
        TSICTestHarness.fx.mockRect(ctx.doc.getElementById('outside'), 20, 80, 600, 200);
        let changes = 0;
        ctx.doc.getElementById('dd').addEventListener('tsic-change', () => changes++);
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.focus.confirm(); await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.domExists(ctx.doc, '.tsic-dropdown-portal'));
        // Simulate outside click.
        ctx.doc.getElementById('outside').dispatchEvent(new ctx.win.MouseEvent('mousedown', { bubbles: true }));
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.eq(ctx.doc.querySelectorAll('.tsic-dropdown-portal').length, 0));
        ctx.expect(ctx.assert.eq(changes, 0));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: focus memory survives re-render of the same screen key',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="x" data-tsic-initial-focus style="position:absolute; left:0;  top:0;">x</button>' +
              '<button id="y"                          style="position:absolute; left:0;  top:50px;">y</button>' +
              '<button id="z"                          style="position:absolute; left:0;  top:100px;">z</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.win.tsic.focus.focus('#z');
        // Re-render the body — same screen key (still 'Inventory').
        ctx.doc.body.innerHTML =
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="x" data-tsic-initial-focus style="position:absolute; left:0; top:0;">x</button>' +
              '<button id="y"                          style="position:absolute; left:0; top:50px;">y</button>' +
              '<button id="z"                          style="position:absolute; left:0; top:100px;">z</button>' +
            '</div>';
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        // Mode flip to trigger applyInitialFocus.
        ctx.mode('MouseAndKeyboard'); await new Promise(r => setTimeout(r, 20));
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'z'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: pages without [data-tsic-initial-focus] still focus something',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
        ctx.doc.body.innerHTML =
            '<div data-tsic-focus-group="t">' +
              '<button id="b1">b1</button><button id="b2">b2</button>' +
            '</div>';
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        const a = ctx.doc.activeElement;
        ctx.expect(ctx.assert.truthy(a && a.id === 'b1',
            'expected first focusable, got ' + (a && a.id)));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: focused button carries [data-tsic-focused] for hover styling',
    file: '/screens/main-menu.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.mode('Gamepad');
        const initial = await TSICTestHarness.fx.awaitInitialFocus(ctx);
        // The initial-focus button must carry the marker.
        ctx.expect(ctx.assert.truthy(initial && initial.hasAttribute('data-tsic-focused'),
            'initial-focus button should carry [data-tsic-focused]'));
        // Navigate and confirm the marker moves.
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        const next = ctx.doc.activeElement;
        ctx.expect(ctx.assert.truthy(next && next !== initial,
            'pressDir(down) should move focus from initial button'));
        ctx.expect(ctx.assert.truthy(next.hasAttribute('data-tsic-focused'),
            'newly focused button should carry [data-tsic-focused]'));
        // Previous button should have lost the marker.
        ctx.expect(ctx.assert.truthy(!initial.hasAttribute('data-tsic-focused'),
            'previously focused button should have lost [data-tsic-focused]'));
        // Mouse mode clears the marker so :hover styles don't double up.
        ctx.mode('MouseAndKeyboard');
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.eq(ctx.doc.querySelectorAll('[data-tsic-focused]').length, 0));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: only one element carries [data-tsic-focused] at a time',
    file: '/screens/main-menu.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(ctx.doc.querySelectorAll('[data-tsic-focused]').length, 1));
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        // Still exactly one marker after navigation.
        ctx.expect(ctx.assert.eq(ctx.doc.querySelectorAll('[data-tsic-focused]').length, 1));
    },
});

TSICTestHarness.register({
    // Regression: in the playground's INPUT pane, every direction button
    // lives in the host page outside the iframe. Clicking one moves browser
    // focus to that host button, which resets the iframe's activeElement to
    // <body>. step() must then resume from the engine's own marker
    // ([data-tsic-focused]) — not bounce back to initial-focus on every press.
    name: 'Focus/Stress: nav resumes from marker after activeElement reverts to body',
    file: '/screens/main-menu.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.mode('Gamepad');
        const initial = await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.truthy(initial, 'expected initial focus to land'));
        // Move off the initial button so we have a distinct "current" target.
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        const moved = ctx.doc.activeElement;
        ctx.expect(ctx.assert.truthy(moved && moved !== initial,
            'pressDir(down) should leave initial; activeElement=' + (moved && moved.id)));
        ctx.expect(ctx.assert.truthy(moved.hasAttribute('data-tsic-focused'),
            'engine should have stamped [data-tsic-focused] on the new active element'));
        // Simulate the iframe losing focus to a host-page click — the browser
        // resets activeElement to <body>, but the engine's marker stays put.
        moved.blur();
        await new Promise(r => setTimeout(r, 10));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement, ctx.doc.body,
            'precondition: blur should leave activeElement on <body>'));
        ctx.expect(ctx.assert.truthy(moved.hasAttribute('data-tsic-focused'),
            'precondition: focused marker should survive blur'));
        // Press a direction — focus must continue from the marker, not the
        // initial button. (Before the fix, step() saw activeElement=<body>,
        // didn't find it in candidates, and fell straight back to initial.)
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        const after = ctx.doc.activeElement;
        ctx.expect(ctx.assert.truthy(after && after !== initial,
            'after blur+nav, focus must NOT bounce to initial; got ' + (after && after.id)));
        ctx.expect(ctx.assert.truthy(after !== moved,
            'after blur+nav, focus should advance past the marker; got ' + (after && after.id)));
    },
});

TSICTestHarness.register({
    name: 'Focus/Stress: full mode → confirm → cancel cycle',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="open" data-tsic-initial-focus style="position:absolute; left:0; top:0;">open</button>' +
              '<div id="popup" style="position:absolute; left:200px; top:200px;">' +
                '<button id="action" style="position:absolute; left:200px; top:200px;">action</button>' +
              '</div>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        let actionFired = 0;
        ctx.doc.getElementById('action').addEventListener('click', () => actionFired++);
        ctx.doc.getElementById('open').addEventListener('click', () => {
            ctx.win.tsic.focus.pushScope(ctx.doc.getElementById('popup'), '#action');
        });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad'); await new Promise(r => setTimeout(r, 40));
        // Press confirm: open clicked, scope pushed onto popup, action focused.
        ctx.focus.confirm(); await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'action'));
        // Press confirm again: action clicked.
        ctx.focus.confirm(); await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(actionFired, 1));
        // Press cancel: scope pops, focus returns to open.
        ctx.focus.cancel(); await new Promise(r => setTimeout(r, 15));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'open'));
    },
});

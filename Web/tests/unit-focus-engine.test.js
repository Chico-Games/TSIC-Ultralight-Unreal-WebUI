// Unit tests for shared/tsic-focus.js — the focus engine.
//
// Fixtures use deterministic rects mocked via TSICTestHarness.fx (so the
// spatial-nearest math has predictable inputs in jsdom and the same scenarios
// still work under a real browser where rects come from real layout).

TSICTestHarness.register({
    name: 'Focus/Engine: stamps data-tsic-input from UI.Input.Mode.Changed',
    file: '/screens/inventory.html',
    async run(ctx) {
        await ctx.waitFor(() => ctx.doc.documentElement.getAttribute('data-tsic-input'));
        ctx.expect(ctx.assert.eq(
            ctx.doc.documentElement.getAttribute('data-tsic-input'),
            'MouseAndKeyboard'));
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.eq(
            ctx.doc.documentElement.getAttribute('data-tsic-input'),
            'Gamepad'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: initial focus picks [data-tsic-initial-focus] on Gamepad',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t">' +
              '<button id="t1" data-tsic-initial-focus>One</button>' +
              '<button id="t2">Two</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 't1'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: per-screen memory restores last-focused on re-enable',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t">' +
              '<button id="m1" data-tsic-initial-focus>One</button>' +
              '<button id="m2">Two</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.win.tsic.focus.focus('#m2');
        ctx.mode('MouseAndKeyboard');
        await new Promise(r => setTimeout(r, 30));
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'm2'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: spatial nav picks nearest in direction (3x3 grid)',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div id="grid" data-tsic-focus-group="g" style="position:fixed; inset:0;">' +
              '<button id="b00" style="position:absolute; left:50px;  top:50px;">00</button>' +
              '<button id="b10" style="position:absolute; left:150px; top:50px;">10</button>' +
              '<button id="b20" style="position:absolute; left:250px; top:50px;">20</button>' +
              '<button id="b01" style="position:absolute; left:50px;  top:150px;">01</button>' +
              '<button id="b11" style="position:absolute; left:150px; top:150px;" data-tsic-initial-focus>11</button>' +
              '<button id="b21" style="position:absolute; left:250px; top:150px;">21</button>' +
              '<button id="b02" style="position:absolute; left:50px;  top:250px;">02</button>' +
              '<button id="b12" style="position:absolute; left:150px; top:250px;">12</button>' +
              '<button id="b22" style="position:absolute; left:250px; top:250px;">22</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b11'));
        ctx.focus.pressDir('right'); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b21'));
        ctx.focus.pressDir('down');  await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b22'));
        ctx.focus.pressDir('left');  await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b12'));
        ctx.focus.pressDir('up');    await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b11'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: pressing past the edge is a no-op (no wrap)',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="g" style="position:fixed; inset:0;">' +
              '<button id="top"    style="position:absolute; left:50px; top:50px;"  data-tsic-initial-focus>top</button>' +
              '<button id="bottom" style="position:absolute; left:50px; top:150px;">bottom</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.focus.pressDir('up'); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'top'));
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'bottom'));
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'bottom'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: ConfirmAccept clicks the focused element',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t"><button id="bx" data-tsic-initial-focus>X</button></div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        let clicked = 0;
        ctx.doc.getElementById('bx').addEventListener('click', () => clicked++);
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(clicked, 1));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: pushScope constrains nav and popScope restores caller',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="outer" style="position:fixed; inset:0;">' +
              '<button id="o1" data-tsic-initial-focus style="position:absolute; left:20px;  top:20px;">O1</button>' +
              '<button id="o2"                       style="position:absolute; left:120px; top:20px;">O2</button>' +
              '<div id="modal" data-tsic-focus-group="modal" style="position:absolute; left:200px; top:200px;">' +
                '<button id="m1" style="position:absolute; left:200px; top:200px;">M1</button>' +
                '<button id="m2" style="position:absolute; left:200px; top:240px;">M2</button>' +
              '</div>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.win.tsic.focus.focus('#o1');
        ctx.win.tsic.focus.pushScope(ctx.doc.getElementById('modal'), '#m1');
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'm1'));
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'm2'));
        ctx.win.tsic.focus.popScope();
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'o1'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: CancelBack pops scope but is a no-op at top level',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="outer">' +
              '<button id="x" data-tsic-initial-focus>x</button>' +
              '<div id="m" data-tsic-focus-group="m"><button id="m1">m1</button></div>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.focus.cancel(); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'x'));
        ctx.win.tsic.focus.pushScope(ctx.doc.getElementById('m'), '#m1');
        await new Promise(r => setTimeout(r, 20));
        ctx.focus.cancel(); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'x'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: data-tsic-skip-focus excludes element from nav',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" style="position:absolute; left:20px;  top:20px;" data-tsic-initial-focus>a</button>' +
              '<button id="b" style="position:absolute; left:120px; top:20px;" data-tsic-skip-focus>b</button>' +
              '<button id="c" style="position:absolute; left:220px; top:20px;">c</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.focus.pressDir('right'); await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'c'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: zero-size elements are filtered from the focusable set',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t">' +
              '<button id="vis" data-tsic-initial-focus>vis</button>' +
              '<button id="hiddenAttr" hidden>hidden</button>' +
              '<button id="ariaHidden" aria-hidden="true">aria</button>' +
              '<button id="disabledBtn" disabled>disabled</button>' +
              '<button id="zero" style="width:0; height:0;">zero</button>' +
            '</div>');
        TSICTestHarness.fx.mockRect(ctx.doc.getElementById('vis'), 0, 0, 100, 28);
        // Force the zero-size button to keep a literal zero rect even in real
        // layout (a 0-width button still naturally measures zero, but make it
        // explicit so the test is deterministic).
        TSICTestHarness.fx.mockRect(ctx.doc.getElementById('zero'), 0, 100, 0, 0);
        ctx.win.tsic.focus.enable();
        const set = ctx.win.tsic.focus.__focusableSet();
        const ids = set.map(el => el.id);
        ctx.expect(ctx.assert.truthy(ids.includes('vis'), 'expected vis to be focusable'));
        ctx.expect(ctx.assert.truthy(!ids.includes('hiddenAttr'),  'expected [hidden] NOT focusable'));
        ctx.expect(ctx.assert.truthy(!ids.includes('ariaHidden'),  'expected [aria-hidden] NOT focusable'));
        ctx.expect(ctx.assert.truthy(!ids.includes('disabledBtn'), 'expected [disabled] NOT focusable'));
        ctx.expect(ctx.assert.truthy(!ids.includes('zero'),        'expected zero-rect NOT focusable'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: Gamepad mode publishes setInteractiveRects([])',
    file: '/screens/inventory.html',
    async run(ctx) {
        let lastRects = 'unset';
        ctx.win.tsic.setInteractiveRects = (rects) => { lastRects = rects; };
        TSICTestHarness.fx.setupFixture(ctx, '<div data-tsic-focus-group="t"><button id="x" data-tsic-initial-focus>x</button></div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.eq(lastRects, []));
        ctx.mode('MouseAndKeyboard');
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.truthy(Array.isArray(lastRects) && lastRects.length === 1));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: scrolls container when focused row falls below viewport',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
        const rows = Array.from({ length: 30 }, (_, i) =>
            '<div data-tsic-focusable id="r' + i + '">' + i + '</div>'
        ).join('');
        ctx.doc.body.innerHTML =
            '<div id="scroller" data-tsic-focus-group="rows">' +
              rows.replace('id="r0"', 'id="r0" data-tsic-initial-focus') +
            '</div>';
        const scroller = ctx.doc.getElementById('scroller');
        Object.defineProperty(scroller, 'scrollHeight', { value: 30 * 28, configurable: true });
        Object.defineProperty(scroller, 'clientHeight', { value: 200, configurable: true });
        const cs = ctx.win.getComputedStyle;
        ctx.win.getComputedStyle = (el) => {
            if (el === scroller) return { overflowY: 'auto' };
            return cs ? cs.call(ctx.win, el) : { overflowY: 'visible' };
        };
        TSICTestHarness.fx.mockRect(scroller, 0, 0, 200, 200);
        const rowEls = ctx.doc.querySelectorAll('#scroller > div');
        rowEls.forEach((el, i) => TSICTestHarness.fx.mockRect(el, 0, i * 28, 200, 28));
        let scrollDelta = 0;
        scroller.scrollBy = ({ top }) => { scrollDelta += top; };
        ctx.focus.disableSmoothScroll();
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        scrollDelta = 0;
        ctx.win.tsic.focus.focus('#r15');
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.truthy(scrollDelta > 0,
            'expected scroll to advance downward, got scrollDelta=' + scrollDelta));
    },
});

TSICTestHarness.register({
    name: 'Focus/Engine: Started always fires; Triggered burst is throttled',
    file: '/screens/inventory.html',
    async run(ctx) {
        TSICTestHarness.fx.setupFixture(ctx,
            '<div data-tsic-focus-group="t" style="position:fixed; inset:0;">' +
              '<button id="a" style="position:absolute; left:20px; top:20px;"  data-tsic-initial-focus>a</button>' +
              '<button id="b" style="position:absolute; left:20px; top:120px;">b</button>' +
              '<button id="c" style="position:absolute; left:20px; top:220px;">c</button>' +
            '</div>');
        TSICTestHarness.fx.applyRects(ctx.doc.querySelectorAll('button'), { onlyIfZeroRect: true });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.input('IA_UI_Navigate', 'Started', { X: 0, Y: -1, Z: 0 });
        await new Promise(r => setTimeout(r, 20));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'b'));
        for (let i = 0; i < 5; i++) {
            ctx.input('IA_UI_Navigate', 'Triggered', { X: 0, Y: -1, Z: 0 });
        }
        await new Promise(r => setTimeout(r, 30));
        const idAfterBurst = ctx.doc.activeElement.id;
        ctx.expect(ctx.assert.truthy(
            idAfterBurst === 'b' || idAfterBurst === 'c',
            'expected b or c after Triggered burst, got ' + idAfterBurst));
    },
});

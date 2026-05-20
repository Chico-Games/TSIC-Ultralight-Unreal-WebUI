// Unit tests for shared/context-menu.js (window.TSICContextMenu namespace).

TSICTestHarness.register({
    name: 'Unit/ContextMenu: namespace installed with open + close',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.expect(ctx.assert.truthy(ctx.win.TSICContextMenu, 'expected window.TSICContextMenu'));
        ctx.expect(ctx.assert.truthy(typeof ctx.win.TSICContextMenu.open === 'function'));
        ctx.expect(ctx.assert.truthy(typeof ctx.win.TSICContextMenu.close === 'function'));
    },
});

TSICTestHarness.register({
    name: 'Unit/ContextMenu: open renders entries; click fires callback and closes',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        let fired = false;
        ctx.win.TSICContextMenu.open({
            x: 10, y: 10,
            entries: [
                { label: 'Hello', onClick: () => { fired = true; } },
                { label: 'World', disabled: true, onClick: () => {} },
            ],
        });
        const items = ctx.doc.querySelectorAll('.tsic-context-menu .tsic-context-item');
        ctx.expect(ctx.assert.eq(items.length, 2));
        // Click the first item.
        items[0].click();
        ctx.expect(ctx.assert.eq(fired, true));
        ctx.expect(ctx.assert.eq(ctx.doc.querySelector('.tsic-context-menu'), null, 'menu should have closed'));
    },
});

TSICTestHarness.register({
    name: 'Unit/ContextMenu: Escape closes the menu',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.win.TSICContextMenu.open({ x: 5, y: 5, entries: [{ label: 'A', onClick: () => {} }] });
        ctx.expect(ctx.assert.domExists(ctx.doc, '.tsic-context-menu'));
        ctx.win.dispatchEvent(new ctx.win.KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true }));
        ctx.expect(ctx.assert.eq(ctx.doc.querySelector('.tsic-context-menu'), null));
    },
});

TSICTestHarness.register({
    name: 'Unit/ContextMenu: outside mousedown closes the menu',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.win.TSICContextMenu.open({ x: 5, y: 5, entries: [{ label: 'A', onClick: () => {} }] });
        ctx.expect(ctx.assert.domExists(ctx.doc, '.tsic-context-menu'));
        // Dispatch a mousedown on document body (outside the panel).
        ctx.doc.body.dispatchEvent(new ctx.win.MouseEvent('mousedown', { bubbles: true }));
        ctx.expect(ctx.assert.eq(ctx.doc.querySelector('.tsic-context-menu'), null));
    },
});

TSICTestHarness.register({
    name: 'Unit/ContextMenu: open() with empty entries is a no-op',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.win.TSICContextMenu.open({ x: 0, y: 0, entries: [] });
        ctx.expect(ctx.assert.eq(ctx.doc.querySelector('.tsic-context-menu'), null));
    },
});

// Unit tests for shared/router.js — meta-tag parsers, publishMenuContext.
TSICTestHarness.register({
    name: 'Unit/Router: exposes publishMenuContext helper on window',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.expect(ctx.assert.truthy(typeof ctx.win.__tsicPublishMenuActionContext === 'function',
            'expected window.__tsicPublishMenuActionContext to be installed by router.js'));
    },
});

TSICTestHarness.register({
    name: 'Unit/Router: publishMenuContext auto-injects Back row',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.clearPublishes();
        ctx.win.__tsicPublishMenuActionContext([
            { ActionName: 'IA_UI_ConfirmAccept', Label: 'Craft', Priority: 10 },
        ]);
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.ActionBar.SetMenuContext', {
            where: p => p.Entries.some(e => e.ActionName === 'IA_UI_CancelBack' && e.Label === 'Back'),
        }));
    },
});

TSICTestHarness.register({
    name: 'Unit/Router: explicit IA_UI_CancelBack entry overrides auto-Back',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.clearPublishes();
        ctx.win.__tsicPublishMenuActionContext([
            { ActionName: 'IA_UI_CancelBack', Label: 'Resume', Priority: 1000 },
        ]);
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.ActionBar.SetMenuContext', {
            where: p => p.Entries.length === 1 && p.Entries[0].Label === 'Resume',
        }));
    },
});

TSICTestHarness.register({
    name: 'Unit/Router: publishMenuContext de-dups by ActionName',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.clearPublishes();
        ctx.win.__tsicPublishMenuActionContext([
            { ActionName: 'IA_X', Label: 'first',  Priority: 1 },
            { ActionName: 'IA_X', Label: 'second', Priority: 2 },
        ]);
        const last = ctx.publishes().slice(-1)[0];
        const xs = last.payload.Entries.filter(e => e.ActionName === 'IA_X');
        ctx.expect(ctx.assert.eq(xs.length, 1));
        ctx.expect(ctx.assert.eq(xs[0].Label, 'first'));
    },
});

TSICTestHarness.register({
    name: 'Unit/Router: empty / falsy entries are skipped',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.clearPublishes();
        ctx.win.__tsicPublishMenuActionContext([
            null,
            { ActionName: '', Label: 'no-action' },
            { ActionName: 'IA_OK', Label: 'ok', Priority: 5 },
        ]);
        const last = ctx.publishes().slice(-1)[0];
        // Auto-Back is appended after de-dup, so size is OK-entry + Back = 2.
        ctx.expect(ctx.assert.eq(last.payload.Entries.length, 2));
        ctx.expect(ctx.assert.truthy(last.payload.Entries.find(e => e.ActionName === 'IA_OK')));
        ctx.expect(ctx.assert.truthy(last.payload.Entries.find(e => e.ActionName === 'IA_UI_CancelBack')));
    },
});

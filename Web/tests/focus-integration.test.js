// Integration flow tests — exercise the focus engine against real menu
// pages with real (mocked) gameplay messages. These hit the same code paths
// a player on a controller would: enter Gamepad mode, navigate to a button,
// confirm, navigate inside a sub-modal, cancel back, etc.

TSICTestHarness.register({
    name: 'Focus/Integration/Pause: confirm on initial button publishes Resume',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.inject('tsic.msg.UI.Players.List', { Players: [{ Name: 'Host', bIsHost: true }] });
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'btn-resume'));
        ctx.clearPublishes();
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.Pause.Resume'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration/Pause: down reaches the settings/quit row, confirm publishes',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.inject('tsic.msg.UI.Players.List', { Players: [] });
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        const a = ctx.doc.activeElement;
        ctx.expect(ctx.assert.truthy(['btn-settings', 'btn-menu'].includes(a.id),
            'down from resume should reach settings or quit; got ' + a.id));
        ctx.clearPublishes();
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 30));
        const expected = (a.id === 'btn-settings') ? 'UI.Cmd.Pause.Settings' : 'UI.Cmd.Pause.QuitToMenu';
        ctx.expect(ctx.assert.published(ctx.handle, expected));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration/MainMenu: navigate to a sub-nav and confirm publishes Navigate',
    file: '/screens/main-menu.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'btn-start'));
        // Press down twice — should reach btn-mods (third in the list).
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'btn-mods'));
        ctx.clearPublishes();
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.Menu.Navigate',
            { where: p => p && p.Screen === 'Mods' }));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration/QuantityPicker: confirm publishes Transfer with chosen count',
    file: '/screens/quantity-picker.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'btn-confirm'));
        ctx.clearPublishes();
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.Inventory.Transfer'));
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.Pause.Resume'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration/Teleporter: confirm on a destination publishes Travel',
    file: '/screens/teleporter.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.inject('tsic.msg.UI.Teleporter.Destinations', {
            Destinations: [
                { EntityId: 1, Label: 'Hub',  Cooldown: 0 },
                { EntityId: 2, Label: 'Camp', Cooldown: 0 },
            ],
        });
        ctx.mode('Gamepad');
        await ctx.waitFor(() => ctx.doc.querySelector('#rows button'));
        // Initial focus is the cancel button (the only declared
        // [data-tsic-initial-focus] on this page).
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'btn-cancel'));
        // Confirming a destination is the gameplay-critical path — focus it
        // explicitly and assert the click handler publishes Travel.
        const dest = ctx.doc.querySelector('#rows button');
        ctx.expect(ctx.assert.truthy(dest, 'expected at least one destination row'));
        // Force focus past the rect filter — production has real layout so
        // the engine reaches this from the cancel row via spatial nav; in
        // jsdom we just stamp it directly.
        dest.focus();
        await new Promise(r => setTimeout(r, 20));
        ctx.clearPublishes();
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.Teleporter.Travel'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration/Cage: pick a target publishes Selection.Choose',
    file: '/screens/cage.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.inject('tsic.msg.UI.Selection.Opened', { Context: 'Cage', Options: [
            { OptionId: 'cap-A', Label: 'A' },
            { OptionId: 'cap-B', Label: 'B' },
        ] });
        ctx.mode('Gamepad');
        await ctx.waitFor(() => ctx.doc.querySelector('#rows button'));
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        const active = ctx.doc.activeElement;
        ctx.expect(ctx.assert.truthy(active && active.closest && active.closest('#rows'),
            'initial focus should land inside #rows; got ' + (active && active.tagName)));
        ctx.clearPublishes();
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.Selection.Choose',
            { where: p => p && p.OptionId === 'cap-A' }));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration/Settings: catalog renders + initial focus on Back',
    file: '/screens/settings.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.inject('tsic.msg.UI.Settings.Catalog', {
            Json: JSON.stringify({ Pages: [{ Id: 'video', Title: 'Video', Groups: [{
                Id: 'video', Title: 'Video', Settings: [
                    { Key: 'video.fov', Label: 'FOV', Type: 'range', Min: 60, Max: 120, Step: 1, Value: 90 },
                ],
            }] }] }),
        });
        ctx.mode('Gamepad');
        await ctx.waitFor(() => ctx.doc.querySelector('#page input, #page button'),
            { timeout: 1500 }).catch(() => {});
        const initial = await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(initial && initial.id, 'btn-back'));
        // Catalog must have rendered something interactive inside #page.
        const catalogChildren = ctx.doc.querySelectorAll('#page input, #page button, #page [data-tsic-focusable]');
        ctx.expect(ctx.assert.truthy(catalogChildren.length > 0,
            'expected catalog to render at least one interactive control; got ' + catalogChildren.length));
        // Whole-page focusable set must include the rendered controls so the
        // engine can navigate to them on a real layout.
        const all = ctx.win.tsic.focus.__structuralFocusableSet();
        const inCatalog = all.filter(el => el.closest && el.closest('#page'));
        ctx.expect(ctx.assert.truthy(inCatalog.length > 0,
            'expected at least one focusable inside #page; got ' + inCatalog.length));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration: Esc/Cancel does not commit a focused button',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.inject('tsic.msg.UI.Players.List', { Players: [] });
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.clearPublishes();
        // Cancel at top level should NOT click the focused button.
        ctx.focus.cancel();
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.notPublished(ctx.handle, 'UI.Cmd.Pause.Resume'));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration/Credits: a single-button page round-trips back to MainMenu',
    file: '/screens/credits.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'btn-back'));
        ctx.clearPublishes();
        ctx.focus.confirm();
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.published(ctx.handle, 'UI.Cmd.Menu.Navigate',
            { where: p => p && p.Screen === 'MainMenu' }));
    },
});

TSICTestHarness.register({
    name: 'Focus/Integration: mode toggles do not steal focus across pages',
    file: '/screens/credits.html',
    async run(ctx) {
        ctx.focus.disableSmoothScroll();
        ctx.focus.resetMemory();
        // Start in mouse mode — engine should be inert.
        await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement, ctx.doc.body));
        // Flip to Gamepad — focus lands.
        ctx.mode('Gamepad');
        await TSICTestHarness.fx.awaitInitialFocus(ctx);
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'btn-back'));
        // Flip back to mouse — engine clears the ring marker but the browser's
        // focus may still report the same element. The CSS rule keys off
        // [data-tsic-input] so this is enough.
        ctx.mode('MouseAndKeyboard');
        await new Promise(r => setTimeout(r, 30));
        ctx.expect(ctx.assert.eq(
            ctx.doc.documentElement.getAttribute('data-tsic-input'),
            'MouseAndKeyboard'));
    },
});

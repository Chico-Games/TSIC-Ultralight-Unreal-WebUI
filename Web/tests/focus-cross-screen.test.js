// Cross-screen focus smoke.
//
// For every menu screen that opts in via <meta name="tsic-focus" content="enabled">,
// confirm:
//   - the meta tag is present (catches: page was opted in by the spec but the
//     opt-in tag never landed).
//   - a [data-tsic-initial-focus] element exists in the rendered DOM (catches:
//     page declared the meta but didn't tag an initial-focus target).
//   - after ctx.mode('Gamepad'), the engine focuses something (catches:
//     applyInitialFocus didn't run / engine never enabled).
//
// Per-screen reachability (focus-per-screen.test.js) is the deeper test; this
// file's job is to scream "a new menu was added and forgot to wire focus".

const FOCUS_SCREENS = [
    'main-menu','new-store','credits','pause-menu','settings','save-load',
    'universal-storage-setup','boss-summoner','construction',
    'teleporter','cage','selection','bug-report','quantity-picker',
];

for (const sname of FOCUS_SCREENS) {
    TSICTestHarness.register({
        name: `Focus/CrossScreen: ${sname} opts in and lands focus`,
        file: `/screens/${sname}.html`,
        tags: ['focus', 'cross-screen'],
        async run(ctx) {
            // Read screen name from the page itself — the closure variable
            // would shadow window.screen in playwright's standalone eval.
            const m = ctx.doc.querySelector('meta[name="tsic-screen"]');
            const pageName = m ? m.getAttribute('content') : '(unknown)';
            ctx.focus.disableSmoothScroll();
            ctx.focus.resetMemory();
            // Some pages render their initial-focus target only after data
            // arrives — seed minimal payloads where we know the channel.
            const fileName = (ctx.win.location.pathname.split('/').pop() || '').replace('.html', '');
            if (fileName === 'cage' || fileName === 'selection') {
                ctx.inject('tsic.msg.UI.Selection.Opened', {
                    Context: fileName === 'cage' ? 'Cage' : 'Pick',
                    Options: [{ OptionId: 'x', Label: 'x' }],
                });
            } else if (fileName === 'teleporter') {
                ctx.inject('tsic.msg.UI.Teleporter.Destinations', {
                    Destinations: [{ EntityId: 1, Label: 'Hub', Cooldown: 0 }],
                });
            }
            await ctx.waitFor(() => ctx.doc.querySelector('[data-tsic-initial-focus]'),
                { timeout: 1000 }).catch(() => {});
            const meta = ctx.doc.querySelector('meta[name="tsic-focus"][content="enabled"]');
            ctx.expect(ctx.assert.truthy(meta, pageName + ': missing <meta name="tsic-focus" content="enabled">'));
            const initial = ctx.doc.querySelector('[data-tsic-initial-focus]');
            ctx.expect(ctx.assert.truthy(initial, pageName + ': missing [data-tsic-initial-focus]'));
            ctx.mode('Gamepad');
            await new Promise(r => setTimeout(r, 120));
            const active = ctx.doc.activeElement;
            ctx.expect(ctx.assert.truthy(
                active && active !== ctx.doc.body,
                pageName + ': engine did not land focus (active=' + (active ? active.tagName : 'null') + ')'));
        },
    });
}

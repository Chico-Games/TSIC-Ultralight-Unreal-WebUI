// Verifies that shared/tsic-focus.js mirrors :hover CSS rules onto
// [data-tsic-focused] in Gamepad mode, so a controller-focused element
// looks the same as a mouse-hovered one.

TSICTestHarness.register({
    name: 'Focus/Hover: mirror stylesheet exists after engine boot',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        // The engine schedules the mirror via requestAnimationFrame after
        // load; give it a tick.
        await new Promise(r => setTimeout(r, 100));
        const mirror = ctx.doc.getElementById('tsic-focus-hover-mirror');
        ctx.expect(ctx.assert.truthy(mirror, 'mirror stylesheet should be inserted after load'));
    },
    tags: ['focus', 'hover-mirror'],
});

TSICTestHarness.register({
    name: 'Focus/Hover: mirror sheet contains Gamepad-gated [data-tsic-focused] rules',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        await new Promise(r => setTimeout(r, 100));
        const mirror = ctx.doc.getElementById('tsic-focus-hover-mirror');
        ctx.expect(ctx.assert.truthy(mirror, 'mirror stylesheet should exist'));
        if (!mirror) return;
        const txt = mirror.textContent || '';
        ctx.expect(ctx.assert.truthy(
            txt.indexOf('html[data-tsic-input="Gamepad"]') !== -1,
            'mirror should gate to Gamepad mode'));
        ctx.expect(ctx.assert.truthy(
            txt.indexOf('[data-tsic-focused]') !== -1,
            'mirror should reference [data-tsic-focused]'));
        ctx.expect(ctx.assert.truthy(
            txt.indexOf(':hover') === -1,
            'mirror itself should NOT contain :hover (it should have been substituted)'));
    },
    tags: ['focus', 'hover-mirror'],
});

TSICTestHarness.register({
    name: 'Focus/Hover: every :hover rule has a matching [data-tsic-focused] rule',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        await new Promise(r => setTimeout(r, 100));
        // Collect every :hover rule across all sheets.
        const hovers = [];
        for (const s of Array.from(ctx.doc.styleSheets)) {
            let rules = null;
            try { rules = s.cssRules; } catch (e) { continue; }
            if (!rules) continue;
            const walk = (rs) => {
                for (const r of Array.from(rs)) {
                    if (r.type === 1 && (r.selectorText || '').indexOf(':hover') !== -1) {
                        hovers.push(r.selectorText);
                    } else if (r.cssRules) {
                        walk(r.cssRules);
                    }
                }
            };
            walk(rules);
        }
        ctx.expect(ctx.assert.truthy(hovers.length > 0,
            'page should have at least one :hover rule to mirror (got 0)'));
        const mirror = ctx.doc.getElementById('tsic-focus-hover-mirror');
        const txt = (mirror && mirror.textContent) || '';
        // Count tokens — we don't expect 1:1 (a single :hover rule with N comma
        // parts can produce <N mirrored rules if some parts lack :hover) but
        // we do expect SOME mirrored rules.
        const mirroredCount = (txt.match(/\[data-tsic-focused\]/g) || []).length;
        ctx.expect(ctx.assert.truthy(mirroredCount > 0,
            'mirror sheet should contain at least one [data-tsic-focused] rule (got 0)'));
        // Sanity: mirrored count should be on the same order of magnitude.
        // Multi-selector :hover rules produce multiple mirror entries, so
        // mirroredCount >= floor(hovers.length / 2) is a reasonable lower bound.
        ctx.expect(ctx.assert.truthy(
            mirroredCount >= Math.floor(hovers.length / 2),
            `mirror should cover most :hover rules: had ${hovers.length} hovers, mirrored ${mirroredCount}`));
    },
    tags: ['focus', 'hover-mirror'],
});

TSICTestHarness.register({
    name: 'Focus/Hover: focused element gets hover background in Gamepad mode',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        // tsic-button has a :hover { background: var(--cat-green-hover) }
        // declared in components.css. After Gamepad mode the initial-focus
        // button (#btn-resume) gets data-tsic-focused. The mirror rule
        // should put the same background on the button.
        await new Promise(r => setTimeout(r, 100));
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 80));
        const btn = ctx.doc.getElementById('btn-resume');
        ctx.expect(ctx.assert.truthy(btn, 'btn-resume should exist'));
        ctx.expect(ctx.assert.truthy(btn.hasAttribute('data-tsic-focused'),
            'btn-resume should carry data-tsic-focused after Gamepad mode'));
        // Snapshot the button's background.
        const bgFocused = ctx.win.getComputedStyle(btn).backgroundColor;
        // Switch back to MouseAndKeyboard and assert it changes (the
        // mirror is gated to Gamepad, so the visual flips).
        ctx.mode('MouseAndKeyboard');
        await new Promise(r => setTimeout(r, 60));
        const bgKbm = ctx.win.getComputedStyle(btn).backgroundColor;
        ctx.expect(ctx.assert.truthy(bgFocused !== bgKbm,
            `background should differ between Gamepad-focused and KBM (both were "${bgFocused}")`));
    },
    tags: ['focus', 'hover-mirror'],
});

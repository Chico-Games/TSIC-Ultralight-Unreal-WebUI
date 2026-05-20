// Unit tests for shared/tsic-dropdown.js

TSICTestHarness.register({
    name: 'Dropdown/Unit: opens, picks, fires tsic-change, restores caller focus',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
        ctx.doc.body.innerHTML =
            '<div data-tsic-focus-group="t">' +
              '<button id="dd" class="tsic-dropdown" data-tsic-focusable data-tsic-initial-focus ' +
                'data-tsic-options=\'[{"value":"a","label":"A"},{"value":"b","label":"B"}]\' ' +
                'data-tsic-value="a">' +
                '<span class="tsic-dropdown-label">A</span>' +
                '<span class="tsic-dropdown-caret">▾</span>' +
              '</button>' +
            '</div>';
        let lastValue = null;
        ctx.doc.getElementById('dd').addEventListener('tsic-change', (e) => { lastValue = e.detail.value; });
        // Give the dropdown trigger a real rect so the engine can focus it
        // and so the portal can position itself off it.
        const _rDD = ctx.doc.getElementById('dd');
        if (_rDD) _rDD.getBoundingClientRect = () => ({ left: 50, top: 50, width: 120, height: 28, right: 170, bottom: 78, x: 50, y: 50 });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.focus.confirm(); // confirm on a .tsic-dropdown opens the portal (engine clicks it)
        await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.domExists(ctx.doc, '.tsic-dropdown-portal [role="listbox"]'));
        // Down to second option, confirm picks.
        ctx.focus.pressDir('down'); await new Promise(r => setTimeout(r, 30));
        ctx.focus.confirm(); await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.eq(lastValue, 'b'));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'dd'));
        ctx.expect(ctx.assert.eq(ctx.win.tsic.dropdown.get('#dd'), 'b'));
    },
});

TSICTestHarness.register({
    name: 'Dropdown/Unit: cancel closes without committing and restores focus',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
        ctx.doc.body.innerHTML =
            '<div data-tsic-focus-group="t">' +
              '<button id="dd" class="tsic-dropdown" data-tsic-focusable data-tsic-initial-focus ' +
                'data-tsic-options=\'[{"value":"a","label":"A"},{"value":"b","label":"B"}]\' ' +
                'data-tsic-value="a">' +
                'A' +
              '</button>' +
            '</div>';
        // Give the dropdown trigger a real rect so the engine can focus it
        // and so the portal can position itself off it.
        const _rDD = ctx.doc.getElementById('dd');
        if (_rDD) _rDD.getBoundingClientRect = () => ({ left: 50, top: 50, width: 120, height: 28, right: 170, bottom: 78, x: 50, y: 50 });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        let changes = 0;
        ctx.doc.getElementById('dd').addEventListener('tsic-change', () => changes++);
        ctx.focus.confirm(); await new Promise(r => setTimeout(r, 50));
        ctx.focus.cancel();  await new Promise(r => setTimeout(r, 50));
        ctx.expect(ctx.assert.eq(changes, 0));
        ctx.expect(ctx.assert.eq(ctx.doc.activeElement.id, 'dd'));
        ctx.expect(ctx.assert.eq(ctx.doc.querySelectorAll('.tsic-dropdown-portal').length, 0));
    },
});

TSICTestHarness.register({
    name: 'Dropdown/Unit: options() repopulates and keeps selection if still valid',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.body.innerHTML =
            '<button id="dd" class="tsic-dropdown" ' +
              'data-tsic-options=\'[{"value":"a","label":"A"}]\' ' +
              'data-tsic-value="a">' +
              '<span class="tsic-dropdown-label">A</span>' +
            '</button>';
        await new Promise(r => setTimeout(r, 50));
        ctx.win.tsic.dropdown.options('#dd', [
            { value: 'a', label: 'A2' },
            { value: 'c', label: 'C' },
        ]);
        ctx.expect(ctx.assert.eq(ctx.win.tsic.dropdown.get('#dd'), 'a'));
        ctx.expect(ctx.assert.eq(ctx.doc.querySelector('#dd .tsic-dropdown-label').textContent, 'A2'));
        // Re-populate without 'a' — selection should be cleared.
        ctx.win.tsic.dropdown.options('#dd', [{ value: 'c', label: 'C' }]);
        ctx.expect(ctx.assert.eq(ctx.win.tsic.dropdown.get('#dd'), null));
    },
});

TSICTestHarness.register({
    name: 'Dropdown/Unit: re-open with current value initially focused (not first)',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.doc.head.insertAdjacentHTML('beforeend', '<meta name="tsic-focus" content="enabled">');
        ctx.doc.body.innerHTML =
            '<div data-tsic-focus-group="t">' +
              '<button id="dd" class="tsic-dropdown" data-tsic-focusable data-tsic-initial-focus ' +
                'data-tsic-options=\'[{"value":"a","label":"A"},{"value":"b","label":"B"},{"value":"c","label":"C"}]\' ' +
                'data-tsic-value="b">' +
                '<span class="tsic-dropdown-label">B</span>' +
              '</button>' +
            '</div>';
        // Give the dropdown trigger a real rect so the engine can focus it
        // and so the portal can position itself off it.
        const _rDD = ctx.doc.getElementById('dd');
        if (_rDD) _rDD.getBoundingClientRect = () => ({ left: 50, top: 50, width: 120, height: 28, right: 170, bottom: 78, x: 50, y: 50 });
        ctx.win.tsic.focus.enable();
        ctx.mode('Gamepad');
        await new Promise(r => setTimeout(r, 50));
        ctx.focus.confirm(); await new Promise(r => setTimeout(r, 50));
        const focused = ctx.doc.activeElement;
        ctx.expect(ctx.assert.eq(focused.getAttribute('role'), 'option'));
        ctx.expect(ctx.assert.eq(focused.dataset.value, 'b'));
        ctx.expect(ctx.assert.eq(focused.getAttribute('aria-selected'), 'true'));
    },
});

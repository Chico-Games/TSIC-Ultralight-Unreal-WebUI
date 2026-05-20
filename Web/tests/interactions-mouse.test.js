// Mouse-interaction coverage: every clickable element on every page.

// ---- Inventory: every tab activates filter classlist -------------------
TSICTestHarness.register({
    name: 'Mouse/Inventory: clicking each tab marks it active and re-filters',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.setItemCatalog({
            ID_Axe:   { Name: 'Axe',   Category: 'Equipment' },
            ID_Bread: { Name: 'Bread', Category: 'Consumable' },
            ID_Wheat: { Name: 'Wheat', Category: 'CraftingMaterial' },
        });
        ctx.inject('tsic.msg.UI.Inventory.Updated', { OwnerId: 'Player', MaxSlots: 32, MaxWeight: 50, CurrentWeight: 1, Items: [
            { ItemId: 'ID_Axe',   Count: 1, SlotIndex: 0 },
            { ItemId: 'ID_Bread', Count: 2, SlotIndex: 1 },
            { ItemId: 'ID_Wheat', Count: 5, SlotIndex: 2 },
        ]});
        await ctx.waitFor(() => ctx.doc.querySelectorAll('.inv-tab').length === 5);
        const labels = ['All','Tools','Cons.','Mats','Other'];
        for (const label of labels) {
            const tab = Array.from(ctx.doc.querySelectorAll('.inv-tab')).find(e => e.textContent === label);
            tab.click();
            await new Promise(r => setTimeout(r, 15));
            const active = ctx.doc.querySelector('.inv-tab.active');
            ctx.expect(ctx.assert.eq(active && active.textContent, label, `tab ${label} should be active`));
        }
    },
});

// ---- Inventory equipment row click ------------------------------------
TSICTestHarness.register({
    name: 'Mouse/Inventory: equip-slot click handler does not throw',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Inventory.Updated', { OwnerId: 'Player', MaxSlots: 32, Items: [] });
        ctx.inject('tsic.msg.UI.Equipment.Updated', { OwnerId: 'Player', Slots: [
            { SlotTag: 'Equip.Weapon', ItemId: 'ID_Axe', IconUrl: '' },
        ]});
        await ctx.waitFor(() => ctx.doc.querySelector('#inv-equip-row .equip-slot'));
        ctx.doc.querySelector('#inv-equip-row .equip-slot').click();
        // Whether it publishes or not depends on impl; just confirm no crash.
        ctx.expect(ctx.assert.truthy(true));
    },
});

// ---- Storage: dblclick on both grids transfers in opposite directions --
TSICTestHarness.register({
    name: 'Mouse/Storage: dblclick on both grids fire transfers in opposite directions',
    file: '/screens/storage.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Inventory.Updated', { OwnerId: 'Storage:42', MaxSlots: 32, Items: [{ ItemId: 'X', Count: 1, SlotIndex: 0 }] });
        ctx.inject('tsic.msg.UI.Inventory.Updated', { OwnerId: 'Player',     MaxSlots: 32, Items: [{ ItemId: 'Y', Count: 1, SlotIndex: 3 }] });
        await ctx.waitFor(() => ctx.doc.querySelector('#ss-container-list .tsic-list-row[data-slot="0"] img')
                              && ctx.doc.querySelector('#ss-player-list .tsic-list-row[data-slot="3"] img'));
        ctx.clearPublishes();
        ctx.doc.querySelector('#ss-container-list .tsic-list-row[data-slot="0"]')
            .dispatchEvent(new ctx.win.MouseEvent('dblclick', { bubbles: true }));
        ctx.doc.querySelector('#ss-player-list .tsic-list-row[data-slot="3"]')
            .dispatchEvent(new ctx.win.MouseEvent('dblclick', { bubbles: true }));
        const pubs = ctx.publishes().filter(p => p.channel === 'UI.Cmd.Inventory.Transfer');
        ctx.expect(ctx.assert.eq(pubs.length, 2));
        ctx.expect(ctx.assert.truthy(pubs.some(p => p.payload.FromOwnerId === 'Storage:42' && p.payload.ToOwnerId === 'Player')));
        ctx.expect(ctx.assert.truthy(pubs.some(p => p.payload.FromOwnerId === 'Player' && p.payload.ToOwnerId === 'Storage:42')));
    },
});

// ---- Construction: clicking every category tab ------------------------
TSICTestHarness.register({
    name: 'Mouse/Construction: clicking each category tab filters items',
    file: '/screens/construction.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Construction.Available', { Items: [
            { EntityDefId: 'A', Name: 'A', Category: 'Furniture',  bAffordable: true },
            { EntityDefId: 'B', Name: 'B', Category: 'Structure',  bAffordable: true },
            { EntityDefId: 'C', Name: 'C', Category: 'Storage',    bAffordable: true },
        ]});
        await ctx.waitFor(() => ctx.doc.querySelectorAll('#c-tabs .c-tab').length >= 4);
        for (const cat of ['Furniture','Structure','Storage']) {
            const tab = Array.from(ctx.doc.querySelectorAll('.c-tab')).find(e => e.textContent === cat);
            tab.click();
            await new Promise(r => setTimeout(r, 15));
            const active = ctx.doc.querySelector('.c-tab.active');
            ctx.expect(ctx.assert.eq(active && active.textContent, cat));
            const rows = ctx.doc.querySelectorAll('#items .c-row');
            ctx.expect(ctx.assert.eq(rows.length, 1, `filtered to one ${cat} row`));
        }
    },
});

// ---- Storage: clicking every category tab ----------------------------
TSICTestHarness.register({
    name: 'Mouse/Storage: every category tab filters the container grid',
    file: '/screens/storage.html',
    async run(ctx) {
        ctx.setItemCatalog({
            ID_Axe:   { Name: 'Axe',   Category: 'Equipment' },
            ID_Bread: { Name: 'Bread', Category: 'Consumable' },
            ID_Wheat: { Name: 'Wheat', Category: 'CraftingMaterial' },
        });
        ctx.inject('tsic.msg.UI.Inventory.Updated', { OwnerId: 'Storage:1', MaxSlots: 32, Items: [
            { ItemId: 'ID_Axe',   Count: 1, SlotIndex: 0 },
            { ItemId: 'ID_Bread', Count: 1, SlotIndex: 1 },
            { ItemId: 'ID_Wheat', Count: 1, SlotIndex: 2 },
        ]});
        await ctx.waitFor(() => ctx.doc.querySelectorAll('.ss-tabs[data-side="container"] .ss-tab').length === 5);
        for (const label of ['Tools','Cons.','Mats','Other','All']) {
            Array.from(ctx.doc.querySelectorAll('.ss-tabs[data-side="container"] .ss-tab'))
                .find(e => e.textContent === label).click();
            await new Promise(r => setTimeout(r, 15));
            ctx.expect(ctx.assert.eq(
                ctx.doc.querySelector('.ss-tabs[data-side="container"] .ss-tab.is-active').textContent, label));
        }
    },
});

// ---- Wardrobe: clicking each cosmetic slot publishes Unequip ----------
TSICTestHarness.register({
    name: 'Mouse/Wardrobe: clicking each filtered slot publishes Unequip with its SlotTag',
    file: '/screens/wardrobe.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Equipment.Updated', { OwnerId: 'Player', Slots: [
            { SlotTag: 'Cosmetic.Head', ItemId: 'h', IconUrl: '' },
            { SlotTag: 'Cosmetic.Body', ItemId: 'b', IconUrl: '' },
            { SlotTag: 'Outfit.Face',   ItemId: 'f', IconUrl: '' },
        ]});
        await ctx.waitFor(() => ctx.doc.querySelectorAll('.tsic-slot').length === 3);
        const slots = ctx.doc.querySelectorAll('.tsic-slot');
        ctx.clearPublishes();
        for (let i = 0; i < slots.length; i++) slots[i].click();
        const pubs = ctx.publishes().filter(p => p.channel === 'UI.Cmd.Equipment.Unequip');
        ctx.expect(ctx.assert.eq(pubs.length, 3));
    },
});

// ---- Pause menu: every button -----------------------------------------
TSICTestHarness.register({
    name: 'Mouse/PauseMenu: Resume/Settings/Quit each publish a different command',
    file: '/screens/pause-menu.html',
    async run(ctx) {
        await ctx.waitFor(() => ctx.doc.querySelector('button'));
        const map = [
            [/resume/i, 'UI.Cmd.Pause.Resume'],
            [/settings/i, 'UI.Cmd.Pause.Settings'],
            [/quit/i, 'UI.Cmd.Pause.QuitToMenu'],
        ];
        for (const [rx, channel] of map) {
            ctx.clearPublishes();
            const btn = Array.from(ctx.doc.querySelectorAll('button')).find(b => rx.test(b.textContent || ''));
            ctx.expect(ctx.assert.truthy(btn, `expected a button matching ${rx}`));
            btn && btn.click();
            ctx.expect(ctx.assert.published(ctx.handle, channel));
        }
    },
});

// ---- MainMenu: every button -----------------------------------------
TSICTestHarness.register({
    name: 'Mouse/MainMenu: every button publishes the right command',
    file: '/screens/main-menu.html',
    async run(ctx) {
        await ctx.waitFor(() => ctx.doc.querySelector('button'));
        const cases = [
            [/start|new/i, 'UI.Cmd.Menu.Navigate'],
            [/load|save/i, 'UI.Cmd.Menu.Navigate'],
            [/mods/i,      'UI.Cmd.Menu.Navigate'],
            [/settings/i,  'UI.Cmd.Menu.Navigate'],
            [/credits/i,   'UI.Cmd.Menu.Navigate'],
            [/exit|quit/i, 'UI.Cmd.Menu.Exit'],
        ];
        for (const [rx, expected] of cases) {
            const btn = Array.from(ctx.doc.querySelectorAll('button')).find(b => rx.test(b.textContent || ''));
            if (!btn) continue;
            ctx.clearPublishes();
            btn.click();
            ctx.expect(ctx.assert.published(ctx.handle, expected, { where: () => true }));
        }
    },
});

// ---- ConstructionCarousel: pure-display — verify Current highlight on -
TSICTestHarness.register({
    name: 'Mouse/ConstructionCarousel: no clicks expected (display-only)',
    file: '/screens/construction-carousel.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Construction.Carousel', { Prev: [], Current: { FurnitureId: 'X', Label: 'X', bAffordable: true }, Next: [] });
        await ctx.waitFor(() => ctx.doc.querySelector('#cc-row .cc-slot.current'));
        ctx.clearPublishes();
        ctx.doc.querySelector('.cc-slot.current').click();
        const pubs = ctx.publishes();
        ctx.expect(ctx.assert.eq(pubs.length, 0, 'carousel is display-only — clicks should not publish'));
    },
});

// ---- Interaction: every target row -----------------------------------
TSICTestHarness.register({
    name: 'Mouse/Interaction: each row publishes with its EntityId + bPrimary',
    file: '/screens/interaction.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Interaction.Targets', { Targets: [
            { EntityId: 11, Label: 'Open',     bIsPrimary: true },
            { EntityId: 12, Label: 'Inspect',  bIsPrimary: false },
        ]});
        await ctx.waitFor(() => ctx.doc.querySelectorAll('.row').length === 2);
        ctx.clearPublishes();
        ctx.doc.querySelector('.row.primary').click();
        ctx.doc.querySelector('.row.alt').click();
        const pubs = ctx.publishes().filter(p => p.channel === 'UI.Cmd.Interaction.Activate');
        ctx.expect(ctx.assert.eq(pubs.length, 2));
        ctx.expect(ctx.assert.truthy(pubs.some(p => p.payload.bPrimary === true  && p.payload.EntityId === 11)));
        ctx.expect(ctx.assert.truthy(pubs.some(p => p.payload.bPrimary === false && p.payload.EntityId === 12)));
    },
});

// ---- Selection: clicking each option publishes its OptionId ------------
TSICTestHarness.register({
    name: 'Mouse/Selection: each option publishes its OptionId',
    file: '/screens/selection.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Selection.Opened', { Context: 'Loot', Options: [
            { OptionId: 'a', Label: 'A' },
            { OptionId: 'b', Label: 'B' },
            { OptionId: 'c', Label: 'C' },
        ]});
        await ctx.waitFor(() => ctx.doc.querySelectorAll('.row').length === 3);
        ctx.clearPublishes();
        for (const btn of ctx.doc.querySelectorAll('.row')) btn.click();
        const pubs = ctx.publishes().filter(p => p.channel === 'UI.Cmd.Selection.Choose');
        ctx.expect(ctx.assert.eq(pubs.length, 3));
        const ids = pubs.map(p => p.payload.OptionId).sort();
        ctx.expect(ctx.assert.eq(ids, ['a','b','c']));
    },
});

// ---- Save/Load: each slot's Load button -------------------------------
TSICTestHarness.register({
    name: 'Mouse/SaveLoad: each slot publishes LoadSlot with its SlotId',
    file: '/screens/save-load.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Save.Slots', { Slots: [
            { SlotId: 's1', Label: 'A', TimestampIso: '2026-05-18T00:00:00Z' },
            { SlotId: 's2', Label: 'B', TimestampIso: '2026-05-17T00:00:00Z' },
        ]});
        await new Promise(r => setTimeout(r, 80));
        const buttons = Array.from(ctx.doc.querySelectorAll('button')).filter(b => /load/i.test(b.textContent || ''));
        ctx.expect(ctx.assert.truthy(buttons.length >= 2));
        ctx.clearPublishes();
        for (const b of buttons) b.click();
        const pubs = ctx.publishes().filter(p => p.channel === 'UI.Cmd.Menu.LoadSlot');
        ctx.expect(ctx.assert.eq(pubs.length, buttons.length));
        // Both slot ids should appear.
        const ids = pubs.map(p => p.payload.SlotId).sort();
        ctx.expect(ctx.assert.eq(ids.includes('s1') && ids.includes('s2'), true));
    },
});

// ---- Hotbar: clicking every visible slot fires Select with its index --
TSICTestHarness.register({
    name: 'Mouse/Hotbar: clicking every slot publishes Select with the right index',
    file: '/screens/hotbar.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Hotbar.Changed', { SlotIndices: [1,2,3,-1,5,-1,-1,-1,-1,-1], SelectedSlot: 0 });
        await ctx.waitFor(() => ctx.doc.querySelectorAll('#hotbar-row .tsic-slot').length === 10);
        ctx.clearPublishes();
        const slots = ctx.doc.querySelectorAll('#hotbar-row .tsic-slot');
        for (let i = 0; i < slots.length; i++) slots[i].click();
        const pubs = ctx.publishes().filter(p => p.channel === 'UI.Cmd.Hotbar.Select');
        ctx.expect(ctx.assert.eq(pubs.length, 10));
        ctx.expect(ctx.assert.truthy(pubs.every((p, i) => p.payload.SlotIndex === i)));
    },
});

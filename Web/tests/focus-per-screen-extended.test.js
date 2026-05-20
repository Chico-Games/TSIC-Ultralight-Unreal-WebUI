// Focus reachability tests for the 14 menu screens opted into the focus
// engine in the 2026-05-19 expansion: crafting, inventory, production,
// repair, upgrade, storage, universal-storage, wardrobe, cheat-menu, mods,
// death-screen, equipment, interaction, paper.
//
// Each fixture seeds the channels the page needs to render its interactive
// content (so reachability has something to BFS across), then asserts:
//   A) every focusable element is reachable via 4-way nav from initial focus
//   B) every focus group is reachable from every other group
//   C) any dropdowns round-trip cleanly

function focusOpts(extraTags) {
    return { tags: ['focus', 'reachability'].concat(extraTags || []) };
}

// -- crafting --------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Crafting: reachable + groups mutually reachable',
    file: '/screens/crafting.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Item.Catalog', { Items: [
            { ItemId: 'ID_Wheat', Name: 'Wheat', Category: 'CraftingMaterial', Weight: 0.05 },
            { ItemId: 'ID_Bread', Name: 'Bread', Category: 'Consumable',       Weight: 0.20 },
        ] });
        ctx.inject('tsic.msg.UI.Recipe.StationOpened', {
            Kind: 'Crafting', StationId: 'S_Workbench',
            Recipes: [
                { RecipeId: 'R_Bread', Name: 'Bread', bDiscovered: true, bStationLevelSufficient: true,
                  Ingredients: [{ ItemId: 'ID_Wheat', Count: 2 }],
                  Outputs:     [{ ItemId: 'ID_Bread', Count: 1 }], Duration: 3 },
                { RecipeId: 'R_Loaf',  Name: 'Loaf',  bDiscovered: true, bStationLevelSufficient: true,
                  Ingredients: [{ ItemId: 'ID_Wheat', Count: 3 }],
                  Outputs:     [{ ItemId: 'ID_Bread', Count: 2 }], Duration: 5 },
            ],
            MaterialCounts: { ID_Wheat: 6 },
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- inventory -------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Inventory: reachable + groups mutually reachable',
    file: '/screens/inventory.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Item.Catalog', { Items: [
            { ItemId: 'ID_Axe',   Name: 'Axe',   Category: 'Equipment',         Weight: 1.2 },
            { ItemId: 'ID_Bread', Name: 'Bread', Category: 'Consumable',        Weight: 0.2 },
            { ItemId: 'ID_Wheat', Name: 'Wheat', Category: 'CraftingMaterial',  Weight: 0.05 },
        ] });
        ctx.inject('tsic.msg.UI.Inventory.Updated', {
            OwnerId: 'Player', MaxSlots: 32, MaxWeight: 30, CurrentWeight: 2,
            Items: [
                { ItemId: 'ID_Axe',   Count: 1, SlotIndex: 0 },
                { ItemId: 'ID_Bread', Count: 3, SlotIndex: 1 },
                { ItemId: 'ID_Wheat', Count: 8, SlotIndex: 2 },
            ],
        });
        ctx.inject('tsic.msg.UI.Equipment.Updated', { OwnerId: 'Player', Slots: [] });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- production ------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Production: reachable + groups mutually reachable',
    file: '/screens/production.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Item.Catalog', { Items: [
            { ItemId: 'ID_Wheat', Name: 'Wheat', Category: 'CraftingMaterial' },
            { ItemId: 'ID_Bread', Name: 'Bread', Category: 'Consumable' },
        ] });
        ctx.inject('tsic.msg.UI.Recipe.StationOpened', {
            Kind: 'Production', StationId: 'S_Oven',
            Recipes: [
                { RecipeId: 'R_Bread', Name: 'Bread', bDiscovered: true, bStationLevelSufficient: true,
                  Ingredients: [{ ItemId: 'ID_Wheat', Count: 2 }],
                  Outputs: [{ ItemId: 'ID_Bread', Count: 1 }], Duration: 6 },
            ],
            MaterialCounts: { ID_Wheat: 5 },
        });
        ctx.inject('tsic.msg.UI.Recipe.QueueChanged', {
            Kind: 'Production', StationId: 'S_Oven',
            Entries: [{ QueueIndex: 0, RecipeId: 'R_Bread', Progress: 0.5, bIsActive: true }],
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- repair ----------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Repair: reachable + groups mutually reachable',
    file: '/screens/repair.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Item.Catalog', { Items: [
            { ItemId: 'ID_Axe',  Name: 'Axe',  Category: 'Equipment' },
            { ItemId: 'ID_Wood', Name: 'Wood', Category: 'CraftingMaterial' },
        ] });
        ctx.inject('tsic.msg.UI.Recipe.StationOpened', {
            Kind: 'Repair', StationId: 'S_RepairBench',
            Recipes: [
                { RecipeId: 'ID_Axe', Name: 'Axe', bDiscovered: true, bStationLevelSufficient: true,
                  Ingredients: [{ ItemId: 'ID_Wood', Count: 2 }], Outputs: [],
                  Durability: 0.4, MaxDurability: 1 },
            ],
            MaterialCounts: { ID_Wood: 5 },
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- upgrade ---------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Upgrade: reachable + groups mutually reachable',
    file: '/screens/upgrade.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Item.Catalog', { Items: [
            { ItemId: 'ID_Iron', Name: 'Iron', Category: 'CraftingMaterial' },
        ] });
        ctx.inject('tsic.msg.UI.Recipe.StationOpened', {
            Kind: 'Upgrade', StationId: 'F_Workbench_01',
            Recipes: [
                { RecipeId: 'U_Tier2', Name: 'Tier 2', bDiscovered: true, bStationLevelSufficient: true,
                  Ingredients: [{ ItemId: 'ID_Iron', Count: 4 }] },
                { RecipeId: 'U_Tier3', Name: 'Tier 3', bDiscovered: true, bStationLevelSufficient: true,
                  Ingredients: [{ ItemId: 'ID_Iron', Count: 8 }] },
            ],
            MaterialCounts: { ID_Iron: 6 },
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- storage ---------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Storage: reachable + groups mutually reachable',
    file: '/screens/storage.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Item.Catalog', { Items: [
            { ItemId: 'ID_Wood', Name: 'Wood', Category: 'CraftingMaterial' },
            { ItemId: 'ID_Stone',Name: 'Stone',Category: 'CraftingMaterial' },
        ] });
        ctx.inject('tsic.msg.UI.Inventory.Updated', {
            OwnerId: 'Player', MaxSlots: 32, MaxWeight: 30, CurrentWeight: 1,
            Items: [{ ItemId: 'ID_Wood', Count: 4, SlotIndex: 0 }],
        });
        ctx.inject('tsic.msg.UI.Inventory.Updated', {
            OwnerId: 'Storage:Chest01', MaxSlots: 24, MaxWeight: 200, CurrentWeight: 12,
            Items: [
                { ItemId: 'ID_Wood',  Count: 9, SlotIndex: 0 },
                { ItemId: 'ID_Stone', Count: 4, SlotIndex: 1 },
            ],
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- universal-storage -----------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/UniversalStorage: reachable + groups mutually reachable',
    file: '/screens/universal-storage.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Item.Catalog', { Items: [
            { ItemId: 'ID_Wood', Name: 'Wood', Category: 'CraftingMaterial' },
        ] });
        ctx.inject('tsic.msg.UI.Inventory.Updated', {
            OwnerId: 'Player', MaxSlots: 32, MaxWeight: 30, CurrentWeight: 1,
            Items: [{ ItemId: 'ID_Wood', Count: 3, SlotIndex: 0 }],
        });
        ctx.inject('tsic.msg.UI.Inventory.Updated', {
            OwnerId: 'Universal', MaxSlots: 64, MaxWeight: 500, CurrentWeight: 12,
            Items: [{ ItemId: 'ID_Wood', Count: 24, SlotIndex: 0 }],
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- wardrobe --------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Wardrobe: reachable + groups mutually reachable',
    file: '/screens/wardrobe.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Equipment.Updated', {
            OwnerId: 'Player',
            Slots: [
                { SlotTag: 'Equipment.Cosmetic.Head', ItemId: 'ID_Hat' },
                { SlotTag: 'Equipment.Cosmetic.Body', ItemId: 'ID_Coat' },
            ],
        });
        ctx.inject('tsic.msg.UI.CharacterPreview.Ready', { bReady: true });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- cheat-menu ------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/CheatMenu: reachable + groups mutually reachable',
    file: '/screens/cheat-menu.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Cheat.Catalog', {
            Items: [
                { InternalName: 'ID_Bread', DisplayName: 'Bread' },
                { InternalName: 'ID_Wood',  DisplayName: 'Wood' },
            ],
            Creatures: [{ InternalName: 'CH_Mimic', DisplayName: 'Mimic' }],
            FurnitureDefault:     [{ InternalName: 'FD_Table',     DisplayName: 'Table' }],
            FurnitureConstructed: [{ InternalName: 'FD_TableCnstr',DisplayName: 'Constr Table' }],
            ConstructionItems:    [{ InternalName: 'CI_Table',     DisplayName: 'CI Table' }],
            Recipes:              [{ InternalName: 'R_Bread',      DisplayName: 'Bread recipe' }],
            Equippables:          [{ InternalName: 'ID_Axe',       DisplayName: 'Axe' }],
            Weapons:              [{ InternalName: 'ID_Axe',       DisplayName: 'Axe' }],
            HeadGear:             [{ InternalName: 'ID_Hat',       DisplayName: 'Hat' }],
            BodyArmor:            [{ InternalName: 'ID_Coat',      DisplayName: 'Coat' }],
            LegArmor:             [{ InternalName: 'ID_Pants',     DisplayName: 'Pants' }],
            Shoes:                [{ InternalName: 'ID_Boots',     DisplayName: 'Boots' }],
            Gloves:               [{ InternalName: 'ID_Gloves',    DisplayName: 'Gloves' }],
        });
        ctx.inject('tsic.msg.UI.Players.List', { Players: [
            { PlayerId: 1, Name: 'Ziggy',  bIsHost: true },
            { PlayerId: 2, Name: 'Friend', bIsHost: false },
        ] });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- mods ------------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Mods: reachable + groups mutually reachable',
    file: '/screens/mods.html',
    async run(ctx) {
        // Mods page disables auth flow when no IoConfig — keeps the test
        // focused on the static button rows.
        ctx.inject('tsic.msg.UI.Mod.IoConfig', { /* empty => disabled state */ });
        ctx.inject('tsic.msg.UI.Mod.InstalledList', { Mods: [] });
        ctx.inject('tsic.msg.UI.Mod.LoadOrder', { Order: [] });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- death-screen ----------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/DeathScreen: reachable + groups mutually reachable',
    file: '/screens/death-screen.html',
    async run(ctx) { await TSICTestHarness.fx.runReachability(ctx); },
}, focusOpts()));

// -- equipment -------------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Equipment: reachable + groups mutually reachable',
    file: '/screens/equipment.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Equipment.Updated', {
            OwnerId: 'Player',
            Slots: [
                { SlotTag: 'Equipment.MainHand', ItemId: 'ID_Axe' },
                { SlotTag: 'Equipment.OffHand',  ItemId: '' },
                { SlotTag: 'Equipment.Head',     ItemId: 'ID_Hat' },
            ],
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- interaction -----------------------------------------------------------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Interaction: reachable + groups mutually reachable',
    file: '/screens/interaction.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.Interaction.Targets', { Targets: [
            { EntityId: 1, Label: 'Open Storage', bIsPrimary: true },
            { EntityId: 1, Label: 'Inspect',      bIsPrimary: false },
            { EntityId: 1, Label: 'Break',        bIsPrimary: false },
        ] });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// -- paper (uses lore.js, label "Lore / Paper" in the playground) ---------
TSICTestHarness.register(Object.assign({
    name: 'Focus/Paper: reachable + groups mutually reachable',
    file: '/screens/paper.html',
    async run(ctx) {
        ctx.inject('tsic.msg.UI.LoreScreen.Opened', {
            ScreenKind: 'Paper',
            Texts: [
                { Heading: 'The Store', Body: 'In the beginning…' },
                { Heading: 'The Stock', Body: 'Items drift…' },
            ],
            InitialIndex: 0,
        });
        await TSICTestHarness.fx.runReachability(ctx);
    },
}, focusOpts()));

// Unit tests for the test harness itself — no SPA page logic, just the mock.
TSICTestHarness.register({
    name: 'Unit/Harness: ctx.inject fires same-channel subscribers',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        let received = null;
        ctx.win.tsic.on('tsic.msg.X.Y', (p) => { received = p; });
        ctx.inject('tsic.msg.X.Y', { a: 1 });
        ctx.expect(ctx.assert.eq(received && received.a, 1));
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: publishMessage records into log + fires same-channel subscribers',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        let received = null;
        ctx.win.tsic.on('Same.Channel', (p) => { received = p; });
        ctx.clearPublishes();
        ctx.win.tsic.publishMessage('Same.Channel', { v: 42 });
        ctx.expect(ctx.assert.eq(received && received.v, 42));
        ctx.expect(ctx.assert.published(ctx.handle, 'Same.Channel', { where: p => p.v === 42 }));
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: sticky cache replays last payload on subscribe',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        // inject() populates stickyCache and fires existing subscribers; here
        // we inject BEFORE subscribing so we can prove the replay path.
        ctx.inject('tsic.msg.Sticky.Test', { value: 7 });
        let received = null;
        ctx.win.tsic.on('tsic.msg.Sticky.Test', (p) => { received = p; });
        ctx.expect(ctx.assert.eq(received && received.value, 7));
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: publish log records every page-side publish',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.clearPublishes();
        ctx.win.tsic.publishMessage('My.Channel.A', { x: 1 });
        ctx.win.tsic.publishMessage('My.Channel.B', { x: 2 });
        const log = ctx.publishes();
        ctx.expect(ctx.assert.eq(log.length, 2));
        ctx.expect(ctx.assert.eq(log[0].channel, 'My.Channel.A'));
        ctx.expect(ctx.assert.eq(log[1].channel, 'My.Channel.B'));
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: unsubscribe stops the callback firing',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        let count = 0;
        const off = ctx.win.tsic.on('tsic.msg.Once', () => { count++; });
        ctx.inject('tsic.msg.Once', {});
        off();
        ctx.inject('tsic.msg.Once', {});
        ctx.expect(ctx.assert.eq(count, 1));
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: ctx.input forges Enhanced-Input message shape',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        let captured = null;
        ctx.win.tsic.on('tsic.msg.UI.Input.IA_TestAction', (p) => { captured = p; });
        ctx.input('IA_TestAction', 'Started', { X: 1, Y: 0, Z: 0 });
        ctx.expect(ctx.assert.eq(captured && captured.Phase, 'Started'));
        ctx.expect(ctx.assert.eq(captured && captured.Action, 'IA_TestAction'));
        ctx.expect(ctx.assert.eq(captured && captured.Value && captured.Value.X, 1));
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: assert.eq matches deeply',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.expect(ctx.assert.eq({ a: [1, 2] }, { a: [1, 2] })); // pass = null
        const err = ctx.assert.eq({ a: 1 }, { a: 2 }, 'compare');
        ctx.expect(ctx.assert.truthy(err && err.indexOf('compare') === 0));
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: assert.published.where filters by payload',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        ctx.win.tsic.publishMessage('Test.Filter', { kind: 'A', n: 1 });
        ctx.win.tsic.publishMessage('Test.Filter', { kind: 'B', n: 2 });
        ctx.expect(ctx.assert.published(ctx.handle, 'Test.Filter', { where: p => p.kind === 'B' }));
        const err = ctx.assert.published(ctx.handle, 'Test.Filter', { where: p => p.kind === 'Z' });
        ctx.expect(ctx.assert.truthy(err));  // expect a failure string for unmatched filter
    },
});

TSICTestHarness.register({
    name: 'Unit/Harness: waitFor resolves on truthy predicate',
    file: '/screens/test-fixtures.html',
    async run(ctx) {
        let n = 0;
        setTimeout(() => { n = 5; }, 40);
        const v = await ctx.waitFor(() => (n === 5 ? 'ok' : false), { timeout: 500 });
        ctx.expect(ctx.assert.eq(v, 'ok'));
    },
});

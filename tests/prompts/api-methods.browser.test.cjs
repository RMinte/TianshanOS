const {test} = require('node:test');
const assert = require('node:assert/strict');
const kit = require('./pagekit.cjs');
kit.install();

test('real browser sends parameterless validation and stats reset as POST', async () => {
    await kit.withPage('zh-CN', call => ({code: 0, data: {method: call.method}}), async ({page, calls}) => {
        await page.evaluate(async () => {
            await api.call('ota.validate');
            await api.call('automation.action.stats.reset', {id: 'fixture'});
            await api.call('system.info');
        });
        for (const endpoint of ['ota/validate', 'automation/action/stats/reset', 'system/info']) {
            assert.ok(calls.some(call => call.endpoint === endpoint && call.method === 'POST'), endpoint);
        }
    });
});

test('browser device fixture rejects disallowed methods before success responder', async () => {
    let reached = false;
    await kit.withPage('en-US', call => {
        if (call.endpoint === 'ota/validate') reached = true;
        return {code: 0};
    }, async ({page}) => {
        const status = await page.evaluate(async () => (await fetch('/api/v1/ota/validate')).status);
        assert.equal(status, 405);
        assert.equal(reached, false);
        const result = await page.evaluate(() => api.call('ota.validate'));
        assert.equal(result.code, 0);
        assert.equal(reached, true);
    });
});

test('actual engine controls send writes once and refresh status with GET', async () => {
    await kit.withPage('en-US', () => ({code: 0, data: {state: 'running'}}), async ({page, calls}) => {
        await page.evaluate(async () => {
            const status = document.createElement('div');
            status.id = 'automation-status';
            document.body.appendChild(status);
            for (const action of ['start', 'stop', 'pause', 'reload']) await automationControl(action);
        });
        assert.deepEqual(calls.filter(call => /^automation\/(start|stop|pause|reload)$/.test(call.endpoint))
            .map(call => [call.endpoint, call.method]),
            ['start', 'stop', 'pause', 'reload'].map(action => [`automation/${action}`, 'POST']));
        assert.equal(calls.filter(call => call.endpoint === 'automation/status' && call.method === 'GET').length, 4);
    });
});

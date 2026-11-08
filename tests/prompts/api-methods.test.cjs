const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const acorn = require('acorn');
const {harness} = require('./harness.cjs');
const {logical: contract} = require('./api-methods.json');
const bits = {GET: 1, POST: 2, PUT: 4, DELETE: 8};

async function client() {
    const h = harness();
    await h.ready();
    h.run(h.read('js/api.js'));
    h.run('api.request = async (endpoint, method, params) => ({endpoint, method, params})');
    return h;
}

test('default calls fit every real registered API, including parameterless writes', async () => {
    const h = await client();
    assert.equal(Object.keys(contract).length, 277);
    for (const [name, methods] of Object.entries(contract)) {
        for (const params of [null, {}, {probe: 'x'}]) {
            const call = await h.run(`api.call(${JSON.stringify(name)}, ${JSON.stringify(params)})`);
            assert.ok(methods & bits[call.method], `${name} rejects default ${call.method}`);
            assert.equal(call.method, 'POST', name);
        }
    }
    for (const name of ['ota.validate', 'automation.action.stats.reset']) {
        assert.equal(contract[name] & bits.GET, 0, `${name} is a write`);
    }
});

test('explicit GET preserves query encoding and carries no body', async () => {
    const h = await client();
    const call = await h.run(`api.call('system.info', {value: 'a &中', zero: 0, no: false, empty: '', absent: null}, 'GET')`);
    assert.equal(call.endpoint, '/system/info?value=a+%26%E4%B8%AD&zero=0&no=false');
    assert.equal(call.method, 'GET');
    assert.equal(call.params, null);
});

test('literal Web UI callers use methods allowed by the actual registry', async () => {
    const h = await client();
    let checked = 0;
    for (const file of ['api.js', 'app.js', 'terminal.js']) {
        // Audit source, including callers removed by optimization.
        const source = fs.readFileSync(path.resolve(__dirname, '../../components/ts_webui/web/js', file), 'utf8');
        const ast = acorn.parse(source, {ecmaVersion: 'latest', locations: true});
        const calls = [];
        function visit(node) {
            if (!node || typeof node !== 'object') return;
            if (node.type === 'CallExpression' && node.callee.type === 'MemberExpression' &&
                node.callee.property.name === 'call' &&
                (node.callee.object.name === 'api' || (file === 'api.js' && node.callee.object.type === 'ThisExpression')) &&
                typeof node.arguments[0]?.value === 'string') calls.push(node);
            for (const value of Object.values(node)) {
                if (Array.isArray(value)) value.forEach(visit);
                else if (value && typeof value === 'object') visit(value);
            }
        }
        visit(ast);
        for (const node of calls) {
            const name = node.arguments[0].value;
            const where = `${file}:${node.loc.start.line} ${name}`;
            assert.ok(Object.hasOwn(contract, name), `${where} is not registered`);
            const method = node.arguments[2];
            assert.ok(!method || method.type === 'Literal', `${where} needs a concrete method audit`);
            const call = await h.run(`api.call(${JSON.stringify(name)}, {}, ${JSON.stringify(method?.value ?? null)})`);
            assert.ok(contract[name] & bits[call.method], `${where} rejects ${call.method}`);
            checked++;
        }
    }
    assert.ok(checked > 150, `Only ${checked} callers audited`);
});

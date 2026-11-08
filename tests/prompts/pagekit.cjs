// A real browser page wired to a fake device, shared by the failure-message suites.
//   const kit = require('./pagekit.cjs');  kit.install();   // registers before/after hooks that start and stop the browser
//   await kit.withPage('zh-CN', call => response, async ({page, calls}) => { ... });
// `call` is {endpoint, method, params}. The response is a raw string (sent verbatim), an object (sent as JSON), undefined
// (an empty success) or kit.DROP (the connection is cut: the device never answers).
const {before, after} = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http'), https = require('node:https'), fs = require('node:fs'), path = require('node:path'), os = require('node:os'), crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');
const {root} = require('./harness.cjs');

const {logical: methodContract, http: routeMethods} = require('./api-methods.json');
const methodBits = {GET: 1, POST: 2, PUT: 4, DELETE: 8};
const DROP = Symbol('drop');
let browser, tlsOptions, tlsDirectory;
function install() {
    before(async () => {
        const args = [];
        if (process.env.TS_TEST_WEB_TLS === '1') {
            tlsDirectory = fs.mkdtempSync(path.join(os.tmpdir(), 'ts-ui-tls-'));
            const key = path.join(tlsDirectory, 'synthetic.key'), cert = path.join(tlsDirectory, 'synthetic.crt');
            const result = spawnSync('openssl', ['req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1', '-nodes', '-days', '2', '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1,DNS:localhost', '-addext', 'basicConstraints=critical,CA:FALSE', '-addext', 'keyUsage=digitalSignature', '-addext', 'extendedKeyUsage=serverAuth', '-keyout', key, '-out', cert], {encoding: 'utf8'});
            assert.equal(result.status, 0, result.stderr);
            fs.chmodSync(key, 0o600);
            tlsOptions = {key: fs.readFileSync(key), cert: fs.readFileSync(cert), minVersion: 'TLSv1.2'};
            const spki = new crypto.X509Certificate(tlsOptions.cert).publicKey.export({type: 'spki', format: 'der'});
            args.push('--ignore-certificate-errors-spki-list=' + crypto.createHash('sha256').update(spki).digest('base64'));
        }
        browser = await chromium.launch({channel: 'chrome', headless: true, args});
    });
    after(async () => {
        try { await browser?.close(); }
        finally { tlsOptions?.key.fill(0); if (tlsDirectory) fs.rmSync(tlsDirectory, {recursive: true, force: true}); }
    });
}

async function open(language, respond = () => undefined, {viewport} = {}) {
    const calls = [], errors = [];
    const handler = (req, res) => {
        const url = new URL(req.url, 'http://local');
        const send = body => { res.statusCode = 200; res.setHeader('Content-Type', 'application/json'); res.end(typeof body === 'string' ? body : JSON.stringify(body)); };
        if (url.pathname.startsWith('/api/v1/')) {
            let raw = '';
            req.setEncoding('utf8');
            req.on('data', part => raw += part);
            req.on('end', () => {
                const call = {endpoint: url.pathname.slice('/api/v1/'.length), method: req.method, params: req.method === 'GET' ? Object.fromEntries(url.searchParams) : (raw && /json/i.test(req.headers['content-type'] || '') ? JSON.parse(raw) : {})};
                calls.push(call);
                const methods = routeMethods[call.endpoint] ?? methodContract[call.endpoint.replaceAll('/', '.')];
                if (methods === undefined || !(methods & methodBits[call.method])) {
                    res.statusCode = methods === undefined ? 404 : 405;
                    res.setHeader('Content-Type', 'application/json');
                    res.end(JSON.stringify({code: 7, error: methods === undefined ? 'not_found' : 'method_not_allowed'}));
                    return;
                }
                const out = respond(call);
                if (out === DROP) { req.socket.destroy(); return; }
                send(out ?? {code: 0, data: {}});
            });
            return;
        }
        const file = path.resolve(root, '.' + (url.pathname === '/' ? '/index.html' : url.pathname));
        if (!file.startsWith(root + '/')) { res.writeHead(403).end(); return; }
        const compressed = process.env.TS_TEST_WEB_GZIP === '1' && fs.existsSync(file + '.gz');
        fs.readFile(compressed ? file + '.gz' : file, (error, data) => {
            if (error) { res.writeHead(404).end(); return; }
            if (compressed) { res.setHeader('Content-Encoding', 'gzip'); res.setHeader('Vary', 'Accept-Encoding'); }
            res.setHeader('Content-Type', ({'.js': 'application/javascript', '.html': 'text/html', '.css': 'text/css', '.woff2': 'font/woff2', '.svg': 'image/svg+xml'})[path.extname(file)] || 'application/octet-stream');
            res.end(data);
        });
    };
    const server = tlsOptions ? https.createServer(tlsOptions, handler) : http.createServer(handler);
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    const base = `${tlsOptions ? 'https' : 'http'}://127.0.0.1:${server.address().port}`;
    let context;
    const close = async () => { await context?.close(); server.closeAllConnections?.(); await new Promise(resolve => server.close(resolve)); };
    try {
    context = await browser.newContext({locale: language, ...(viewport ? {viewport} : {})});
    await context.addInitScript(language => {
        localStorage.setItem('ts_language', language);
        window.__NativeWebSocket = window.WebSocket;
        class Socket { static OPEN = 1; static CONNECTING = 0; constructor(url) { this.url = url; this.readyState = 0; } send() {} close() { this.readyState = 3; } }
        window.WebSocket = Socket;
    }, language);
    const page = await context.newPage();
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(base);
    await page.waitForFunction(() => i18n.isReady());
    await page.evaluate(() => closeLoginModal());
    if (tlsOptions) assert.equal(new URL(page.url()).protocol, 'https:');
    return {page, calls, errors, server, base, close};
    } catch (error) { await close(); throw error; }
}
async function withPage(language, respond, run) {
    const d = await open(language, respond);
    try { await run(d); assert.deepEqual(d.errors, []); } finally { await d.close(); }
}

// failure toasts keep the sentence in .toast-msg; plain toasts keep it directly in #toast
const toastText = page => page.evaluate(() => { const toast = document.getElementById('toast'); return toast?.querySelector('.toast-msg')?.textContent ?? toast?.textContent ?? ''; });
const toastIs = (page, type) => page.waitForFunction(type => document.getElementById('toast')?.classList.contains('toast-' + type) && document.getElementById('toast').classList.contains('show'), type);
const expectToast = async (page, type, text) => {
    await toastIs(page, type);
    await page.waitForFunction(text => { const toast = document.getElementById('toast'); return (toast?.querySelector('.toast-msg')?.textContent ?? toast?.textContent) === text; }, text);
    assert.equal(await toastText(page), text);
};
// the page's own confirmation sheet: answer it the way a user would
const confirmSheet = async (page, yes = true) => {
    const button = page.locator(`.confirm-sheet button[data-r="${yes ? 1 : 0}"]`);
    await button.waitFor();
    await button.click();
};

module.exports = {install, open, withPage, toastText, toastIs, expectToast, confirmSheet, DROP};

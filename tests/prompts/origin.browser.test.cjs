// Real Chrome requests to two loopback origins. Every fixture API/upgrade enters
// the compiled actual SDK dispatcher, Host/Origin/CORS and body-ordering probe.
// Socket/TLS/auth/business and the HTTPD scratch buffer remain modeled boundaries.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const http=require('node:http'),crypto=require('node:crypto'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {spawnSync}=require('node:child_process');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..');
function build(file,origin=''){
 const args=['tests/runtime/test_browser_origin.py','--probe-output',file];if(origin)args.push('--allow-origin',origin);
 const p=spawnSync('python3',args,{cwd:root,encoding:'utf8',env:{...process.env,PYTHONDONTWRITEBYTECODE:'1'}});assert.equal(p.status,0,p.stdout+p.stderr);
}
test('two browser origins: same-origin UI/CLI work; foreign fetch/form/WS produce no business; explicit origin/proxy policy remains available',async()=>{
 const temp=fs.mkdtempSync(path.join(os.tmpdir(),'ts-browser-origins-'));let browser,server,foreign;
 const trace=[],sockets=new Set();let executable=path.join(temp,'default');
 function probe(req,kind){
  const needsAuth=kind!==3&&!req.url.includes('public=1');
  const p=spawnSync(executable,['--probe','http',req.socket.localAddress.replace('::ffff:',''),String(req.socket.localPort),req.headers.host||'ABSENT',req.headers.origin??'ABSENT',String(kind),needsAuth?'1':'0',req.headers.authorization==='Bearer SYNTHETIC_BROWSER_TOKEN'?'1':'0'],{encoding:'utf8'});
  assert.equal(p.status,0,p.stdout+p.stderr);const result=JSON.parse(p.stdout);trace.push({method:kind===2?'WS':req.method,origin:req.headers.origin,authorization:req.headers.authorization,...result});return result;
 }
 function headers(res,p){if(p.acao)res.setHeader('Access-Control-Allow-Origin',p.acao);if(p.vary)res.setHeader('Vary',p.vary);if(p.acam)res.setHeader('Access-Control-Allow-Methods',p.acam);if(p.acah)res.setHeader('Access-Control-Allow-Headers',p.acah);}
 try{
  build(executable);
  server=http.createServer((req,res)=>{
   if(!req.url.startsWith('/probe')){res.writeHead(200,{'Content-Type':'text/html'}).end('<!doctype html><title>Origin policy fixture</title>');return;}
   const p=probe(req,req.url.startsWith('/probe/ws-http')?4:req.method==='OPTIONS'?3:0);headers(res,p);res.statusCode=p.status;
   if(p.ret!==0||p.status===204){res.setHeader('Connection','close');res.end(p.status===204?undefined:'Rejected');return;}
   req.on('data',()=>{});req.on('end',()=>{res.setHeader('Content-Type','application/json');res.end('{"ok":true}');});
  });
  server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s));});
  server.on('upgrade',(req,socket)=>{
   const p=probe(req,2);if(p.ret!==0){socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n');return;}
   const accept=crypto.createHash('sha1').update(req.headers['sec-websocket-key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n\r\n');socket.on('data',()=>socket.destroy());
  });
  foreign=http.createServer((req,res)=>res.writeHead(200,{'Content-Type':'text/html'}).end('<!doctype html><title>Other origin</title>'));
  await Promise.all([new Promise(r=>server.listen(0,'127.0.0.1',r)),new Promise(r=>foreign.listen(0,'127.0.0.1',r))]);
  const device='http://127.0.0.1:'+server.address().port,other='http://127.0.0.1:'+foreign.address().port;
  browser=await chromium.launch({channel:'chrome',headless:true});const page=await browser.newPage();page.setDefaultTimeout(8000);
  const call=()=>page.evaluate(async device=>{try{const r=await fetch(device+'/probe',{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer SYNTHETIC_BROWSER_TOKEN'},body:'{"x":1}'});return {ok:r.ok,body:await r.json()};}catch(e){return {blocked:true};}},device);
  const ws=()=>page.evaluate(device=>new Promise(resolve=>{const s=new WebSocket(device.replace('http:','ws:')+'/ws');const timer=setTimeout(()=>{s.close();resolve('timeout');},5000);s.onopen=()=>{clearTimeout(timer);s.close();resolve('open');};s.onerror=()=>{clearTimeout(timer);resolve('blocked');};}),device);
  await page.goto(device);trace.length=0;assert.deepEqual(await call(),{ok:true,body:{ok:true}});assert.equal(trace.length,1);assert.equal(trace[0].business,1);assert.equal(trace[0].origin,device);assert.equal(trace[0].authorization,'Bearer SYNTHETIC_BROWSER_TOKEN');assert.equal(trace[0].acao,device);
  trace.length=0;const ordinary=await page.evaluate(async()=>{const r=await fetch('/probe/ws-http');return r.status;});assert.equal(ordinary,400);assert.equal(trace.length,1);assert.equal(trace[0].lookup,1);assert.equal(trace[0].upgrade,0);assert.equal(trace[0].business,0);assert.equal(trace[0].allocations,0);assert.equal(trace[0].auth,0);
  trace.length=0;const ordinaryCli=await fetch(device+'/probe/ws-http');assert.equal(ordinaryCli.status,400);assert.equal(trace[0].upgrade,0);assert.equal(trace[0].business,0);
  trace.length=0;assert.equal(await ws(),'open');assert.equal(trace.at(-1).upgrade,1);
  await page.goto(other);trace.length=0;assert.deepEqual(await call(),{blocked:true});assert(trace.some(r=>r.method==='OPTIONS'));assert(trace.every(r=>r.origin===other&&r.ret!==0&&!r.lookup&&!r.auth&&!r.business&&!r.reads&&!r.allocations));
  trace.length=0;const opaque=await page.evaluate(async device=>{const r=await fetch(device+'/probe?public=1',{method:'POST',mode:'no-cors',headers:{'Content-Type':'text/plain'},body:'{"x":1}'});return r.type;},device);assert.equal(opaque,'opaque');assert.equal(trace.length,1);assert.equal(trace[0].method,'POST');assert.equal(trace[0].origin,other);assert.equal(trace[0].business,0);assert.equal(trace[0].reads,0);assert.equal(trace[0].lookup,0);
  trace.length=0;assert.equal(await ws(),'blocked');assert.equal(trace.at(-1).origin,other);assert.equal(trace.at(-1).upgrade,0);assert.equal(trace.at(-1).lookup,0);
  // Same transport without Origin still needs its normal authorization.
  trace.length=0;let cli=await fetch(device+'/probe',{method:'POST',headers:{Authorization:'Bearer SYNTHETIC_BROWSER_TOKEN'},body:'{"x":1}'});assert.equal(cli.status,200);assert.equal(trace[0].origin,undefined);assert.equal(trace[0].acao,'');
  trace.length=0;cli=await fetch(device+'/probe',{method:'POST',body:'{"x":1}'});assert.equal(cli.status,401);assert.equal(trace[0].auth,1);assert.equal(trace[0].business,0);assert.equal(trace[0].reads,0);
  // Grant this precise public origin in a separate compiled policy, never by
  // accepting arbitrary Host/Origin pairs or by changing the browser settings.
  const allowed=path.join(temp,'explicit');build(allowed,other);executable=allowed;
  trace.length=0;assert.deepEqual(await call(),{ok:true,body:{ok:true}});assert(trace.some(r=>r.method==='OPTIONS'&&r.status===204&&!r.business));assert(trace.some(r=>r.method==='POST'&&r.business===1&&r.origin===other&&r.acao===other));
  trace.length=0;assert.equal(await ws(),'open');assert.equal(trace.at(-1).upgrade,1);
  console.log('PASS real Chrome: same-origin API/WS, credentialed cross-origin preflight denial, opaque simple POST zero effects, foreign WS denied before101, CLI auth retained, exact explicit origin API/WS accepted; ordinary GET to WS-only route rejected');
 }finally{
  await browser?.close();for(const s of sockets)s.destroy();await Promise.all([server&&new Promise(r=>server.close(r)),foreign&&new Promise(r=>foreign.close(r))]);fs.rmSync(temp,{recursive:true,force:true});
 }
});

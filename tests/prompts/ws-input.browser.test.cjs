const {test,before,after}=require('node:test');
const assert=require('node:assert/strict');
const http=require('node:http'),crypto=require('node:crypto'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require('playwright');
let server,browser,base;const records=new Map(),sockets=new Set();
before(async()=>{
 server=http.createServer((req,res)=>{
  if(req.url==='/terminal.js'){res.setHeader('Content-Type','application/javascript');res.end(fs.readFileSync(path.resolve(process.env.WS_INPUT_TEST_SOURCE||'components/ts_webui/web/js/terminal.js')));}
  else{res.setHeader('Content-Type','text/html');res.end('<!doctype html><script src="/terminal.js"></script>');}
 });
 server.on('upgrade',(req,socket,head)=>{
  const id=new URL(req.url,'http://local').pathname,accept=crypto.createHash('sha1').update(req.headers['sec-websocket-key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  records.set(id,[]);sockets.add(socket);socket.on('close',()=>sockets.delete(socket));socket.on('error',()=>{});
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n\r\n');
  let pending=Buffer.alloc(0);
  const consume=chunk=>{
   pending=Buffer.concat([pending,chunk]);
   while(pending.length>=2){
    const fin=!!(pending[0]&128),opcode=pending[0]&15,masked=!!(pending[1]&128);let n=pending[1]&127,offset=2;
    if(n===126){if(pending.length<4)return;n=pending.readUInt16BE(2);offset=4;}
    else if(n===127){if(pending.length<10)return;n=Number(pending.readBigUInt64BE(2));offset=10;}
    if(n>1024*1024){socket.destroy();return;}if(pending.length<offset+(masked?4:0)+n)return;
    const mask=masked?pending.subarray(offset,offset+4):null;if(masked)offset+=4;
    const bytes=Buffer.from(pending.subarray(offset,offset+n));if(mask)for(let i=0;i<n;i++)bytes[i]^=mask[i%4];pending=pending.subarray(offset+n);
    if(opcode===8){socket.end(Buffer.from([0x88,0]));return;}
    records.get(id).push({fin,opcode,n,json:JSON.parse(bytes.toString('utf8'))});socket.write(Buffer.from([0x81,2,0x6f,0x6b]));
   }
  };
  socket.on('data',consume);if(head.length)consume(head);
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));base='http://127.0.0.1:'+server.address().port;
 browser=await chromium.launch({channel:'chrome',headless:true});
});
after(async()=>{await browser?.close();for(const socket of sockets)socket.destroy();await new Promise(resolve=>server?.close(resolve));});
for(const kind of ['ASCII','Unicode','escaped controls'])test(kind+': real Chrome sends complete bounded frames without changing SSH input',async()=>{
 const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
 try{
  await page.goto(base);await page.waitForFunction(()=>typeof WebTerminal==='function');
  const result=await page.evaluate(async({url,kind})=>{
   const text=kind==='ASCII'?'pasted command \n'.repeat(25000):kind==='Unicode'?'x'.repeat(2047)+'🚀中文'.repeat(20000):'\x01\x02\n\t"\\'.repeat(20000);
   const socket=new WebSocket(url);await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
   let sent=0,received=0,done;
   const ready=new Promise(resolve=>{done=resolve;});
   socket.onmessage=()=>{received++;if(received===sent)done();};
   const target={ws:{readyState:WebSocket.OPEN,send:data=>{sent++;socket.send(data);}}};
   WebTerminal.prototype.sendSshInput.call(target,text);await ready;socket.close();return {text,sent};
  },{url:base.replace('http:','ws:')+'/'+encodeURIComponent(kind),kind});
  const frames=records.get('/'+encodeURIComponent(kind));assert.equal(frames.length,result.sent);assert(frames.length>1);
  assert(frames.every(f=>f.fin&&f.opcode===1&&f.n<=64*1024&&f.json.type==='ssh_input'));
  assert.equal(frames.map(f=>f.json.data).join(''),result.text);
  assert(frames.every(f=>!/[\uD800-\uDBFF]$/.test(f.json.data)&&! /^[\uDC00-\uDFFF]/.test(f.json.data)));
  assert.deepEqual(errors,[]);
 }finally{await page.close();}
});
test('empty input and disconnected input submit no messages',async()=>{
 const page=await browser.newPage();try{
  await page.goto(base);await page.waitForFunction(()=>typeof WebTerminal==='function');
  assert.equal(await page.evaluate(()=>{let calls=0;const send=()=>calls++;WebTerminal.prototype.sendSshInput.call({ws:{readyState:WebSocket.OPEN,send}},'');WebTerminal.prototype.sendSshInput.call({ws:{readyState:WebSocket.CLOSED,send}},'text');return calls;}),0);
 }finally{await page.close();}
});

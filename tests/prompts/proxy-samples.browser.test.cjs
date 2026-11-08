// Real UI handlers and both languages, synthetic device replies; no remote sockets.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const kit=require('./pagekit.cjs');kit.install();
for(const language of ['zh-CN','en-US'])for(const protocol of ['websocket','socketio'])for(const reason of ['ws_message_too_large','ws_invalid_frame','success']){
 test(`${language} ${protocol}: ${reason} is actionable and source form remains editable`,async()=>{
  const endpoint=protocol==='websocket'?'automation/proxy/websocket_test':'automation/proxy/socketio_test';
  const reply=reason==='success'?{code:0,data:protocol==='websocket'?{connected:true,message:{temperature:23}}:{connected:true,event:'status',data:{temperature:23},available_paths:[{path:'temperature',type:'number',value:23}]}}:{code:7,message:reason,data:{connected:true,error:reason}};
  await kit.withPage(language,call=>call.endpoint===endpoint?reply:undefined,async({page,calls})=>{
   await page.evaluate(protocol=>{api.token='fixture-root';api.level='root';api.username='root';showAddSourceModal();switchSourceType(protocol);document.getElementById(protocol==='websocket'?'source-ws-uri':'source-sio-url').value=protocol==='websocket'?'ws://192.0.2.9:81/feed':'http://192.0.2.9:3000';},protocol);
   await page.evaluate(protocol=>protocol==='websocket'?testWsConnection():testSioConnection(),protocol);
   const box=protocol==='websocket'?'#ws-test-result':'#sio-test-result',shown=await page.locator(box+' .test-status').innerText();
   assert(shown.trim());assert(!/ws_message_too_large|ws_invalid_frame|undefined|NaN/.test(shown));
   if(reason==='ws_message_too_large')assert(shown.includes(language==='zh-CN'?'65536':'64 KiB'));
   if(reason==='ws_invalid_frame')assert(language==='zh-CN'?shown.includes('分帧'):shown.includes('framing'));
   if(reason==='success')assert((await page.locator(protocol==='websocket'?'#ws-json-preview':'#sio-json-preview').innerText()).includes('23'));
   if(protocol==='socketio'&&reason!=='success')assert(!/ws_message_too_large|ws_invalid_frame/.test(await page.locator('#sio-json-preview').innerText()));
   const request=calls.find(call=>call.endpoint===endpoint);assert(request);assert.equal(request.method,'POST');
   assert.equal(await page.locator(protocol==='websocket'?'#source-ws-uri':'#source-sio-url').inputValue(),protocol==='websocket'?'ws://192.0.2.9:81/feed':'http://192.0.2.9:3000');
  });
 });
}

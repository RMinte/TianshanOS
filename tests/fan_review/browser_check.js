async (page) => {
 const base='http://127.0.0.1:58007', results=[],errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{window.WebSocket=class{static OPEN=1;static CONNECTING=0;constructor(){this.readyState=0;}send(){}close(){this.readyState=3;}};});
 await page.route('**/*',async route=>{
  const url=new URL(route.request().url());
  if(url.origin!==base){await route.abort();return;}
  if(url.pathname.startsWith('/api/')){await route.fulfill({json:{code:0,data:{authenticated:true,user:{role:'admin',username:'fixture'}}}});return;}
  await route.continue();
 });
 // JSON records are real source-extracted driver + production API serializer outputs.
 const packets=await (await page.request.get(base+'/evidence/repaired-repro.jsonl')).text();
 const records=packets.trim().split('\n').map(JSON.parse);
 for(const source of ['/', '/candidate/'])for(const language of ['zh-CN','en-US']){
  await page.goto(base+source);await page.evaluate(lang=>localStorage.setItem('ts_language',lang),language);await page.reload();
  await page.waitForFunction(()=>window.i18n?.isReady());await page.evaluate(async()=>{closeLoginModal();await loadSystemPage();});
  for(const r of records.filter(r=>r.case.startsWith('R2_'))){
   await page.evaluate(fan=>updateFanInfo({fans:[fan]}),r.fan);
   const expected=r.fan.duty_valid===false?'--':String(r.case==='R2_inverted'?100-r.hal_output:r.hal_output);
   const big=await page.locator('.fan-speed-num').textContent();if(big!==expected)throw Error(r.case+': '+big+' != '+expected);
   if(await page.locator('.fan-slider').count()!==Number(r.fan.mode==='manual'&&r.fan.enabled!==false))throw Error('slider '+r.case);
   if(expected==='--'&&r.fan.mode==='manual'&&r.fan.enabled!==false&&(!await page.locator('.fan-slider').isDisabled()||await page.locator('.fan-slider-value').textContent()!=='--%'))throw Error('unknown slider');
   if(r.case==='R2_manual_fail'&&!(await page.locator('.fan-request').textContent()).includes('70%'))throw Error('requested value lost');
   results.push({source,language,case:r.case,big});
  }
  const fresh={id:0,mode:'manual',duty:37,duty_valid:true,target_duty:37,enabled:true};
  await page.evaluate(f=>{window.fixtureFan=f;fanSpeedDrafts.clear();window.fanCalls=0;window.fanOutcome='success';api.fanSet=async(id,speed)=>{window.fanCalls++;if(window.fanOutcome==='unknown')throw new Error('fixture timeout');if(window.fanOutcome==='httpfail')throw new ApiOperationError({code:9},{},{httpStatus:500});if(window.fanOutcome==='fail')return{code:9};return{code:0,data:{enabled:true}};};api.call=async()=>({code:0,data:{fans:[window.fixtureFan]}});updateFanInfo({fans:[f]});},fresh);
  const slider=page.locator('.fan-slider');await slider.focus();await slider.evaluate(el=>{el.value='70';el.dispatchEvent(new Event('input',{bubbles:true}));});
  if(await page.locator('.fan-speed-num').textContent()!=='37')throw Error('draft replaced successful output');
  await page.evaluate(()=>{window.originalSlider=document.querySelector('.fan-slider');updateFanInfo({fans:[fixtureFan]});});
  if(!await page.evaluate(()=>originalSlider===document.querySelector('.fan-slider')&&originalSlider.value==='70'))throw Error('poll replaced active draft input');
  if(!(await page.locator('.fan-draft').textContent()).includes('70%'))throw Error('draft label');
  if(await page.evaluate(()=>fanCalls)!==0)throw Error('input sent command');
  await page.screenshot({path:`output/fan-fall-review-repair-20261009/${source==='/'?'source':'built'}-${language}-draft.png`,fullPage:true});
  for(const outcome of ['fail','httpfail','unknown','success']){
   await page.evaluate(()=>document.querySelector('#toast.show .toast-close')?.click());
   await page.evaluate(outcome=>{fanOutcome=outcome;fixtureFan.duty=outcome==='success'?70:37;fixtureFan.target_duty=outcome==='success'?70:37;updateFanInfo({fans:[fixtureFan]});},outcome);
   await page.evaluate(async()=>{await setFanSpeed(0,70);});
   const toast=await page.locator('#toast').evaluate(el=>el.querySelector('.toast-msg')?.textContent??el.textContent);
   const expected=outcome==='success'?(language==='zh-CN'?'风扇0已设为70%。':'Fan 0 set to 70%.'):outcome==='unknown'?(language==='zh-CN'?'设置风扇 0 转速的结果未确认':'The result of setting the speed of fan 0 is unconfirmed'):(language==='zh-CN'?'设置风扇 0 的转速失败':'Failed to set the speed of fan 0');
   if(!toast.includes(expected)||toast.includes('fixture')||toast.includes('fan.setting'))throw Error(outcome+' toast: '+toast);
   const big=await page.locator('.fan-speed-num').textContent();if(big!==(outcome==='unknown'?'--':outcome==='success'?'70':'37'))throw Error(outcome+' output: '+big);
   if(outcome==='unknown'&&(!await page.locator('.fan-slider').isDisabled()||await page.locator('.fan-draft').textContent()!==''))throw Error('unknown keeps active draft');
   results.push({source,language,case:outcome,toast,big});
  }
  if(await page.evaluate(()=>fanCalls)!==4)throw Error('unexpected repeated command');
  await page.evaluate(async()=>{api.call=async()=>{throw Error('fixture status timeout');};await refreshFans();});
  if(await page.locator('.fan-speed-num').textContent()!=='--'||await page.locator('.fan-temperature b').textContent()!=='-- °C')throw Error('old reading presented after status failure');
  results.push({source,language,case:'status read failure: old output invalidated'});
  await page.evaluate(async()=>{
   updateFanInfo({fans:[fixtureFan]});
   api.fanSet=async()=>{
    fixtureFan.enabled=false;fixtureFan.duty=0;fixtureFan.target_duty=70;
    return {code:0,data:{enabled:false}};
   };
   api.call=async()=>({code:0,data:{fans:[fixtureFan]}});await setFanSpeed(0,70);
  });
  const disabledToast=await page.locator('#toast').textContent();
  if(!disabledToast.includes(language==='zh-CN'?'当前处于停用状态':'currently disabled')||await page.locator('.fan-speed-num').textContent()!=='0'||await slider.count())throw Error('disabled setting misreported as applied 70');
  results.push({source,language,case:'disabled success',toast:disabledToast});
  for(const mode of ['auto','curve','manual','off']){
   await page.evaluate(mode=>updateFanInfo({fans:[{id:0,mode,target_duty:70,enabled:true}]}),mode);
   if(await page.locator('.fan-speed-num').textContent()!=='--'||await slider.count()!==Number(mode==='manual'))throw Error('missing output in '+mode);
   if(mode==='manual'&&(!await slider.isDisabled()||await page.locator('.fan-slider-value').textContent()!=='--%'))throw Error('missing manual output');
   results.push({source,language,case:'unknown_'+mode});
  }
  await page.setViewportSize({width:390,height:844});await page.evaluate(()=>updateFanInfo({fans:[fixtureFan]}));
  const overflow=await page.locator('.fan-card').evaluate(el=>el.scrollWidth>el.clientWidth+1);if(overflow)throw Error('mobile fan card overflow');
  results.push({source,language,case:'390px layout',overflow});await page.setViewportSize({width:1280,height:900});
  const help=await page.evaluate(()=>{showFanAutoHelpModal();const txt=document.getElementById('fan-auto-help-modal').textContent;closeFanAutoHelpModal();return txt;});
  if(!help||help.includes('fanPage.'))throw Error('help translation');

  const liveResult = await page.evaluate(() => {
   const manual = {id:0,mode:'manual',enabled:true,duty:37,duty_valid:true};
   const other = {id:1,mode:'manual',enabled:true,duty:48,duty_valid:true};
   updateFanInfo({fans:[other,manual],temperature:49.5,temp_valid:true});
   const active = document.getElementById('fan-slider-0');
   active.focus();
   active.value = '65';
   active.dispatchEvent(new Event('input',{bubbles:true}));
   updateFanInfo({fans:[{...other,mode:'off',duty:0},manual],temperature:50.5,temp_valid:true});
   const focused = active === document.activeElement && active.value === '65';
   const otherIsOff = document.querySelector('[data-fan-id="1"]').dataset.mode === 'off' &&
    !document.getElementById('fan-slider-1');
   const temp = document.querySelector('.fan-temperature b').textContent;
   const temperatureCount = document.querySelectorAll('.fan-temperature').length;
   updateFanInfo({fans:[manual]});
   const missingTemp = document.querySelector('.fan-temperature b').textContent;
   return {focused,otherIsOff,temp,temperatureCount,missingTemp};
  });
  if(!liveResult.focused||!liveResult.otherIsOff||liveResult.temp!=='50.5 °C'||
      liveResult.temperatureCount!==1||liveResult.missingTemp!=='-- °C')throw Error(JSON.stringify(liveResult));
  results.push({source,language,case:'active slider, other card and shared temperature',...liveResult});

  const modeResult = await page.evaluate(async () => {
   const manual = {id:0,mode:'manual',enabled:true,duty:37,duty_valid:true};
   const off = {...manual,mode:'off',duty:0};
   const status = fan => ({code:0,data:{fans:[fan],temperature:49.5,temp_valid:true}});
   updateFanInfo(status(manual).data);
   let completeOldRead, completeModeWrite;
   let reads = 0, writes = 0, modeWrites = 0;
   api.fanSet = async () => { writes++; return {code:0}; };
   api.call = async endpoint => {
    if(endpoint==='fan.mode'){
     if(++modeWrites>1)return {code:0};
     return new Promise(resolve => { completeModeWrite = resolve; });
    }
    if(endpoint!=='fan.status')throw Error(endpoint);
    if(++reads===1)return new Promise(resolve => { completeOldRead = resolve; });
    return status(off);
   };
   const oldRead = refreshFans();
   document.querySelector('.fan-modes [data-mode="off"]').focus();
   const switchMode = setFanMode(0,'off');
   await Promise.resolve();
   const disabledWhilePending = document.getElementById('fan-slider-0').disabled;
   const curve = document.querySelector('.fan-modes [data-mode="curve"]');
   const buttonsBusy = [...document.querySelectorAll('.fan-modes button')].every(
    button => button.getAttribute('aria-disabled') === 'true');
   curve.click();
   await setFanSpeed(0,70);
   completeModeWrite({code:9,error:'fan_config_save_failed',data:{id:0,mode:'off',applied:true,persisted:false}});
   await switchMode;
   const buttonsAvailable = !document.querySelector('.fan-modes [aria-disabled]');
   const focusPreserved = document.activeElement?.dataset.mode === 'off';
   completeOldRead(status(manual));
   await oldRead;
   updateFanInfo(status(manual).data,true);
   const stayedOff = document.querySelector('.fan-card').dataset.mode==='off' &&
    document.querySelector('.fan-speed-num').textContent==='0' && !document.getElementById('fan-slider-0');
   return {disabledWhilePending,buttonsBusy,buttonsAvailable,focusPreserved,writes,modeWrites,stayedOff,toast:document.getElementById('toast').textContent};
  });
  const modeToast = language==='zh-CN'?'风扇模式已切换，但未保存。':'The fan mode changed, but could not be saved.';
  if(!modeResult.disabledWhilePending||!modeResult.buttonsBusy||!modeResult.buttonsAvailable||!modeResult.focusPreserved||
      modeResult.writes||modeResult.modeWrites!==1||!modeResult.stayedOff||!modeResult.toast.includes(modeToast))
   throw Error(JSON.stringify(modeResult));
  results.push({source,language,case:'saved-mode failure and late status protection',...modeResult});

  const settled = await page.evaluate(async () => {
   const manual = {id:0,mode:'manual',enabled:true,duty:37,duty_valid:true};
   const off = {...manual,mode:'off',duty:0};
   const order = [];
   let finishSpeed;
   updateFanInfo({fans:[manual]});
   api.fanSet = async () => {
    order.push('speed');
    return new Promise(resolve => { finishSpeed = resolve; });
   };
   api.call = async endpoint => {
    if(endpoint==='fan.mode'){order.push('mode');return {code:0};}
    if(endpoint==='fan.status')return {code:0,data:{fans:[off]}};
    throw Error(endpoint);
   };
   const speed = setFanSpeed(0,70);
   const mode = setFanMode(0,'off');
   await Promise.resolve();
   const blocked = order.join(',')==='speed';
   finishSpeed({code:0,data:{enabled:true}});
   await Promise.all([speed,mode]);
   return {blocked,order,mode:document.querySelector('.fan-card').dataset.mode};
  });
  if(!settled.blocked||settled.order.join(',')!=='speed,mode'||settled.mode!=='off')throw Error(JSON.stringify(settled));
  results.push({source,language,case:'mode waits for earlier same-fan speed write',...settled});

  const overlapping = await page.evaluate(async () => {
   const manual = id => ({id,mode:'manual',duty:37+id,duty_valid:true,enabled:true});
   const off = {...manual(0),mode:'off',duty:0};
   const fans = [off,manual(1)];
   const card = id => document.querySelector(`.fan-card[data-fan-id="${id}"]`);
   const duty = id => card(id).querySelector('.fan-speed-num').textContent;
   updateFanInfo({fans:[manual(0),manual(1)]});
   let rejectSpeed, finishRead;
   let writes = 0;
   api.fanSet = () => {
    writes++;
    return new Promise((_, reject) => { rejectSpeed = reject; });
   };
   api.call = async endpoint => {
    if(endpoint==='fan.mode')return {code:0,data:{mode:'off',applied:true,persisted:true}};
    if(endpoint==='fan.status')return new Promise(resolve => { finishRead = resolve; });
    throw Error(endpoint);
   };
   const speed = setFanSpeed(1,70);
   const mode = setFanMode(0,'off');
   await new Promise(resolve => setTimeout(resolve,0));
   if(!finishRead)throw Error('mode status read missing');
   const otherButtonsUsable = !card(1).querySelector('.fan-modes [aria-disabled]');
   rejectSpeed(new ApiOperationError({message:'fixture timeout'},'fan.set',{kind:'timeout',uncertain:true}));
   await speed;
   finishRead({code:0,data:{fans,temperature:49.5,temp_valid:true}});
   await mode;
   const confirmed = [card(0).dataset.mode,duty(0),duty(1)];
   const confirmedButtonsUsable = !card(0).querySelector('.fan-modes [aria-disabled]');
   updateFanInfo({fans,temperature:50,temp_valid:true},true);
   return {confirmed,otherButtonsUsable,confirmedButtonsUsable,recovered:duty(1),writes};
  });
  if(overlapping.confirmed.join(',')!=='off,0,--'||!overlapping.otherButtonsUsable||
      !overlapping.confirmedButtonsUsable||overlapping.recovered!=='38'||overlapping.writes!==1)
   throw Error(JSON.stringify(overlapping));
  results.push({source,language,case:'one fan timeout preserves other fan confirmation',...overlapping});

  for(const width of [1440,900,390,320]){
   await page.setViewportSize({width,height:900});
   const layouts = await page.evaluate(async () => {
    const modes = ['off','manual','curve','auto'];
    const rows = [];
    for(const mode of modes){
     const fan = {id:0,mode,enabled:true,duty:mode==='off'?0:34,duty_valid:true,auto_state:'baseline',
      guard_temperature:49.5,predicted_temperature:49.6,slope_c_per_min:0.13};
     updateFanInfo({fans:[fan],temperature:49.5,temp_valid:true});
     const panel = document.querySelector('.fan-panel');
     const box = selector => panel.querySelector(selector)?.getBoundingClientRect();
     const controls = box('.slrow,.fan-meta');
     const head = box('.fan-head'), temp = box('.fan-temperature');
     rows.push({mode,height:box('.fan-body').height,panelHeight:panel.getBoundingClientRect().height,
      gap:controls?controls.top-box('.fan-modes').bottom:null,temperatureRight:Math.abs(temp.right-head.right),
      slider:!!box('.slrow'),metrics:!!box('.fan-meta'),
      listPadding:getComputedStyle(panel.querySelector('.fans-grid')).paddingBlockEnd});
    }
    api.call = async () => { throw Error('fixture status timeout'); };
    await refreshFans();
    const head = document.querySelector('.fan-head');
    const unknown = {overflow:head.scrollWidth>head.clientWidth,values:[...document.querySelectorAll('.fan-stat b')].map(el=>el.textContent)};
    const dual = [];
    for(const mode of modes){
     const fans = [0,1].map(id => ({id,mode,enabled:true,duty:mode==='off'?0:34,duty_valid:true,
      auto_state:'baseline',guard_temperature:49.5,predicted_temperature:49.6,slope_c_per_min:0.13}));
     updateFanInfo({fans,temperature:49.5,temp_valid:true});
     const panel = document.querySelector('.fan-panel');
     const headings = [...panel.querySelectorAll('.fan-head')];
     dual.push({height:panel.getBoundingClientRect().height,
      overflow:headings.some(el=>el.scrollWidth>el.clientWidth+1)});
    }
    return {rows,unknown,dual};
   });
   if(new Set(layouts.rows.map(row=>row.panelHeight)).size!==1||
       layouts.rows.some(row=>row.temperatureRight>1||row.listPadding!=='0px'||row.slider!==(row.mode==='manual')||
         row.metrics!==(row.mode==='auto')||(row.gap!==null&&row.gap!==20))||
       layouts.unknown.overflow||layouts.unknown.values.join(',')!=='--,--,--')throw Error(`${source} ${language} ${width} ${JSON.stringify(layouts)}`);
   results.push({source,language,case:'layout '+width,panelHeight:layouts.rows[0].panelHeight});
   if(new Set(layouts.dual.map(row=>row.height)).size!==1||layouts.dual.some(row=>row.overflow))
    throw Error(`${source} ${language} ${width} dual layout ${JSON.stringify(layouts.dual)}`);
   results.push({source,language,case:'two-fan layout '+width,panelHeight:layouts.dual[0].height});
  }
 }
 if(errors.length)throw Error(JSON.stringify(errors));
 return{checks:results.length,results,script_errors:errors,source_and_built_checked:true,mock_only:true};
}

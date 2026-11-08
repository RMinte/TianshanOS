const {test} = require('node:test');
const assert = require('node:assert/strict');
const kit = require('./pagekit.cjs');
const {fixture} = require('./configuration-fixture.cjs');
kit.install();
const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><rect width="20" height="20" fill="green"/><script>parent.__privateFileExecuted=true</script></svg>';
const headers = {'content-type':'image/svg+xml','cache-control':'no-store','content-disposition':'attachment; filename="download"', 'x-content-type-options':'nosniff', 'content-security-policy':"sandbox; default-src 'none'; frame-ancestors 'none'"};
const deferred = () => {let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};};
async function prepare(page) {
    await page.evaluate(() => {
        api.token='synthetic-file-session-A';api.level='root';api.username='root';
        window.fileUrls={created:[],revoked:[]};
        const create=URL.createObjectURL.bind(URL),revoke=URL.revokeObjectURL.bind(URL);
        URL.createObjectURL=blob=>{const url=create(blob);fileUrls.created.push(url);return url;};
        URL.revokeObjectURL=url=>{fileUrls.revoked.push(url);revoke(url);};
    });
    await page.evaluate(()=>loadCommandsPage());
    await page.evaluate(()=>showAddRuleModal());
}
for (const language of ['zh-CN','en-US']) {
    test(`${language}: authenticated file icons, sandboxed SVG and original file download remain usable`,async()=>{
        const base=fixture();
        await kit.withPage(language,base.respond,async({page})=>{
            await prepare(page);
            const requests=[];
            await page.route('**/api/v1/file/download?*',async route=>{
                const request=route.request(),url=new URL(request.url()),path=url.searchParams.get('path');
                requests.push({url:url.href,authorization:request.headers().authorization,path});
                if(path.includes('missing'))return route.fulfill({status:404,contentType:'application/json',body:'{"error":"File not found"}'});
                await route.fulfill({status:200,headers,body:path.includes('invalid')?'<html>not an image</html>':svg});
            });
            const path='/sdcard/images/例 图+"1.svg';
            await page.evaluate(path=>{
                selectedHostId='file-fixture';sshCommands[selectedHostId]=[{id:'icon',name:'Existing command',command:'printf ok',icon:path}];refreshCommandsList();
                updateCmdIconPreview(path);updateRuleIconPreview(path);
            },path);
            await page.waitForFunction(()=>['#commands-list .command-icon img','#cmd-icon-preview img','#rule-icon-preview img'].every(selector=>document.querySelector(selector)?.naturalWidth>0));
            await page.waitForFunction(()=>fileUrls.revoked.length===3);
            assert.equal(requests.length,3);
            for(const request of requests){assert.equal(request.authorization,'Bearer synthetic-file-session-A');assert.equal(request.path,path);assert.deepEqual([...new URL(request.url).searchParams.keys()],['path']);assert(!request.url.includes('synthetic-file-session'));}
            assert.equal(await page.evaluate(()=>window.__privateFileExecuted),undefined);
            const downloadPromise=page.waitForEvent('download');
            const downloadable='/sdcard/例 图+1.svg';
            assert.deepEqual(await page.evaluate(path=>downloadFile(path,true),downloadable),{started:true});
            const download=await downloadPromise;assert.equal(download.suggestedFilename(),'例 图+1.svg');assert.equal(await download.failure(),null);
            await page.evaluate(()=>{updateCmdIconPreview('/sdcard/missing.svg');updateRuleIconPreview('/sdcard/invalid.svg');sshCommands[selectedHostId][0].icon='/sdcard/missing.svg';refreshCommandsList();});
            await page.waitForFunction(()=>document.querySelector('#cmd-icon-preview .preview-placeholder')&&document.querySelector('#rule-icon-preview .preview-placeholder')&&!document.querySelector('#commands-list [data-command-image-note]').hidden);
            assert.equal(await page.locator('#cmd-icon-preview').innerText(),language==='zh-CN'?'加载失败':'Load failed');
            await page.waitForFunction(()=>fileUrls.created.length===fileUrls.revoked.length);
            assert.equal(await page.evaluate(()=>window.__privateFileExecuted),undefined);
        });
    });
    test(`${language}: late private image cannot update a removed node or changed login`,async()=>{
        const base=fixture();
        await kit.withPage(language,base.respond,async({page})=>{
            await prepare(page);
            const holds=new Map();
            await page.route('**/api/v1/file/download?*',async route=>{
                const path=new URL(route.request().url()).searchParams.get('path'),hold=holds.get(path);
                if(hold){hold.seen.resolve();await hold.release.promise;}
                await route.fulfill({status:200,headers,body:svg});
            });
            for(const mode of ['login','removed']){
                const path=`/sdcard/late-${mode}.svg`,hold={seen:deferred(),release:deferred()};holds.set(path,hold);
                await page.evaluate(path=>{const image=document.createElement('img');image.id='late-file-icon';document.body.appendChild(image);window.lateFileLoad=loadFileIcon(image,path,()=>{window.staleFileFailure=true;});},path);
                await hold.seen.promise;
                await page.evaluate(mode=>{if(mode==='login')api.token='synthetic-file-session-B';else document.getElementById('late-file-icon').remove();},mode);
                hold.release.resolve();await page.evaluate(()=>window.lateFileLoad);
                assert.equal(await page.evaluate(()=>fileUrls.created.length),0);assert.equal(await page.evaluate(()=>window.staleFileFailure),undefined);
                await page.evaluate(()=>document.getElementById('late-file-icon')?.remove());
            }
            await page.evaluate(()=>{updateCmdIconPreview('/sdcard/current.svg');});
            await page.waitForFunction(()=>document.querySelector('#cmd-icon-preview img')?.naturalWidth>0&&fileUrls.revoked.length===1);
            assert.equal(await page.evaluate(()=>fileUrls.created.length),1);
        });
    });
}

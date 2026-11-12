const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const path = require('node:path');
const fs = require('node:fs');
const assert = require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.TEST_BROWSER});
 const page=await browser.newPage({viewport:{width:430,height:932}});
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{
   window.saved=[];
   window.ksu={exec(command,options,callback){
     window.saved.push(command);
     let out='';
     if(command.includes("'side-get'"))out='104 109';
     if(command.includes("'get'"))out='0\nnone\nnone\nnone\n\n\n\n';
     if(command.includes("'status'"))out=JSON.stringify({running:true,enabled:true,connected:true});
     setTimeout(()=>window[callback](0,out,''),0);
   }};
 });
 await page.goto('file:///'+path.resolve('package/webroot/index.html').replaceAll('\\','/'));
 await page.waitForFunction(()=>!document.getElementById('save').disabled);
 assert.equal(await page.locator('.gesture').count(),3);
 await page.waitForFunction(()=>!document.getElementById('side-save').disabled);
 await page.locator('input[name="side-upper"][value="109"]').check();
 assert(await page.locator('input[name="side-lower"][value="104"]').isChecked());
 await page.locator('#side-save').click();
 await page.waitForFunction(()=>document.getElementById('side-message').textContent.includes('已保存'));
 assert((await page.evaluate(()=>window.saved)).some(x=>x.includes("'side-save' '109' '104'")));

 await page.locator('#single').click();
 await page.locator('#action-options').getByRole('button',{name:'自定义命令',exact:true}).click();
 await page.locator('.gesture').first().locator('summary').click();
 await page.locator('.gesture').first().getByRole('button',{name:'填入开启无线调试示例',exact:true}).click();
 assert.equal(await page.locator('#single-command').inputValue(),'# 请求开启无线调试，需要连接 Wi-Fi\n# 首次使用可能需要确认网络和配对；开启已在当前平板验证\nsettings put global adb_wifi_enabled 1');
 assert.equal((await page.evaluate(()=>window.saved)).some(x=>x.includes("'save'")),false);
 await page.locator('.gesture').first().locator('summary').click();
 await page.locator('#single-command').fill("printf '%s' '中文 $() `test`'\ninput keyevent 3");
 await page.locator('#enabled').check();
 await page.locator('#save').click();
 await page.waitForFunction(()=>document.getElementById('message').textContent.includes('已保存'));
 const calls=await page.evaluate(()=>window.saved);
 const save=calls.find(x=>x.includes("'save'"));
 assert(save.includes("'1' 'custom' 'none' 'none'"));
 assert(!save.includes('$()')&&!save.includes('中文'));
 assert(save.includes(Buffer.from("printf '%s' '中文 $() `test`'\ninput keyevent 3").toString('base64')));
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 assert.deepEqual(errors,[]);
 fs.mkdirSync('tests/output',{recursive:true});
 await page.screenshot({path:'tests/output/webui-mobile.png',fullPage:true});
 await page.setViewportSize({width:1000,height:900});
 await page.screenshot({path:'tests/output/webui-tablet.png',fullPage:true});
 assert.equal(await page.locator('.credit').textContent(),'Designed by FlowerLing');
 for(const size of [{width:430,height:932},{width:1200,height:750},{width:750,height:430}]){
   await page.setViewportSize(size);
   await page.locator('#long').scrollIntoViewIfNeeded();await page.locator('#long').click();
   const box=await page.locator('#action-dialog').boundingBox();
   assert(box.x>=0&&box.y>=0&&box.x+box.width<=size.width+1&&box.y+box.height<=size.height+1);
   await page.locator('#action-options').getByRole('button',{name:'电源键长按（跟随系统设置）',exact:true}).scrollIntoViewIfNeeded();
   await page.screenshot({path:'tests/output/action-picker-'+size.width+'x'+size.height+'.png'});
   await page.locator('#close-dialog').click();
   assert.equal(await page.locator('#long').evaluate(e=>e.value),'none');
 }
 await browser.close(); console.log('WebUI: load, save, UTF-8 command encoding, shell quoting, mobile layout passed');
})().catch(e=>{console.error(e);process.exit(1)});

// Run with: node tools/test_ui.cjs (requires playwright; see README).
const {chromium}=require('playwright');
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch(process.env.CHROME_PATH?{executablePath:process.env.CHROME_PATH}:{});
 const sizes=[[844,390],[390,844],[667,375],[375,667],[568,320],[320,568]];
 try{for(const [width,height] of sizes){
  const context=await browser.newContext({viewport:{width,height},hasTouch:true});const page=await context.newPage();
  let received=[],active=0,maxActive=0;const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.route('http://webpad.test/**',async route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/state'){
    active++;maxActive=Math.max(maxActive,active);received.push(Object.fromEntries(url.searchParams));
    await new Promise(r=>setTimeout(r,25));
    await route.fulfill({contentType:'application/json',body:JSON.stringify({usb:true,q:0,l:0,r:0})});active--;
   }else await route.fulfill({contentType:'text/html',body:fs.readFileSync(path.join(__dirname,'../src/index.html'),'utf8')});
  });
  await page.goto('http://webpad.test/');await page.waitForTimeout(100);
  const boxes=await page.locator('#pad > button, .cluster button, .system button, .stick').evaluateAll(els=>els.map(el=>{const r=el.getBoundingClientRect();return {name:el.textContent||el.id,x:r.x,y:r.y,w:r.width,h:r.height};}));
  for(let i=0;i<boxes.length;i++){
   const a=boxes[i];assert(a.x>=0&&a.y>=0&&a.x+a.w<=width+.1&&a.y+a.h<=height+.1,'outside viewport: '+a.name);
   for(let j=i+1;j<boxes.length;j++){const b=boxes[j];assert(!(a.x<b.x+b.w-.1&&a.x+a.w>b.x+.1&&a.y<b.y+b.h-.1&&a.y+a.h>b.y+.1),'overlap '+a.name+' / '+b.name);}
  }
  const left=await page.locator('#left').boundingBox(),right=await page.locator('#right').boundingBox();
  const portrait=height>width;
  const point=(b,dx=0,dy=0)=>({x:b.x+b.width/2+(portrait?-dy:dx),y:b.y+b.height/2+(portrait?dx:dy)});
  const cdp=await context.newCDPSession(page);
  const touch=async(type,points)=>cdp.send('Input.dispatchTouchEvent',{type,touchPoints:points.map((p,id)=>({...p,id,radiusX:3,radiusY:3,force:1}))});
  await touch('touchStart',[point(left,8,0),point(right)]);await page.waitForTimeout(60);
  assert.equal(received.at(-1).lx,'128','touch start must not jump');
  await touch('touchMove',[point(left,35,0),point(right,0,-30)]);await page.waitForTimeout(90);
  assert(+received.at(-1).lx>180,'left rightwards mapping');assert(+received.at(-1).ry<70,'right upwards mapping');
  await touch('touchEnd',[]);await page.waitForTimeout(90);
  assert.equal(received.at(-1).lx,'128');assert.equal(received.at(-1).ry,'128');
  const cross=await page.locator('[data-mask="16384"]').boundingBox();
  await touch('touchStart',[point(cross)]);await touch('touchEnd',[]);await page.waitForTimeout(90);
  assert(received.some(s=>(+s.m&16384)!==0),'short taps must reach transport');assert.equal(received.at(-1).m,'0');
  await touch('touchStart',[point(left)]);await touch('touchMove',[point(left,30)]);await page.waitForTimeout(60);
  await touch('touchCancel',[]);await page.waitForTimeout(70);assert.equal(received.at(-1).lx,'128','cancel must center stick');
  await touch('touchStart',[point(right)]);await touch('touchMove',[point(right,25)]);await page.waitForTimeout(60);
  await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.waitForTimeout(70);assert.equal(received.at(-1).rx,'128','blur must center');
  await touch('touchEnd',[]);
  assert.equal(maxActive,1,'requests must not queue concurrently');assert.deepEqual(errors,[]);
  if(width===844||width===390){await page.screenshot({path:path.join(__dirname,'../docs/controller-'+(portrait?'portrait':'landscape')+'.png')});}
  console.log(width+'x'+height+': bounds, no overlaps, dual touch, axis direction, release, taps, single-flight PASS');await context.close();
 }}finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});

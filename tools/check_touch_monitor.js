// Exercise the real browser script without opening a serial port or a browser.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname,'../docs/touch-live.html'),'utf8');
const script = html.split('<script>')[1].split('</script>')[0];
const nodes = new Map();
const drawing = new Proxy({}, {get:()=>()=>{},set:()=>true});
const document = {
  getElementById(id){if(!nodes.has(id))nodes.set(id,{getContext:()=>drawing,textContent:'',disabled:false});return nodes.get(id);},
  querySelectorAll(){return [];}, createElement(){return {click(){}};},
};
const context = vm.createContext({document,navigator:{},TextEncoder,TextDecoder,setTimeout,URL,Blob});
vm.runInContext(script,context);
const run = code=>vm.runInContext(code,context);
const line = value=>run(`receive(${JSON.stringify(value)})`);
const frame = (count,points,map=0)=>`I (20) DISPLAY_TEST: TOUCH2 count=${count} map=${map} targets=3 cells=255 strokes=2 max=2 points=${points} raw=12,34`;
line('\x1b[0;32m'+frame(2,'1:100:150:12,2:400:280:10')+'\x1b[0m');
assert.equal(run('state.count'),2);assert.equal(run('paths.length'),0);
line(frame(2,'2:410:280:11,1:120:160:14'));
assert.equal(run('paths.length'),2);
assert.equal(run('paths[0].id'),2);assert.equal(run('paths[0].ax'),400);
line(frame(0,''));assert.equal(run('lastPoints.size'),0);
line(frame(1,'1:500:200:12'));assert.equal(run('paths.length'),2);
line(frame(1,'1:800:200:12'));assert.equal(run('state.points[0].x'),500);
line(frame(2,'1:500:200:12'));assert.equal(run('state.count'),1);
line('I DISPLAY_TEST: RESULT touch=ESP_OK errors=4 map=0');assert.equal(run('state.errors'),4);
line('I DISPLAY_TEST: RESULT touch=ESP_OK errors=0 mode=partial-bench');
assert(nodes.get('panel-mode').textContent.includes('OTP partial bench'));
line('I DISPLAY_TEST: RESULT touch=ESP_OK errors=0 mode=full-only');
assert(nodes.get('panel-mode').textContent.includes('180'));
line('I DISPLAY_TEST: EPD PERF PARTIAL bytes=800 render=2ms power=12ms spi=1ms busy=305ms off=8ms total=326ms');
assert.equal(run('lastPanelPerformance.bytes'),800);assert.equal(run('lastPanelPerformance.busy'),305);
assert(nodes.get('panel-perf').textContent.includes('326'));
line('I DISPLAY_TEST: TEST_RESET map=2');assert.equal(run('state.map'),2);assert.equal(run('paths.length'),0);assert.equal(run('records.length'),0);
line(frame(1,'1:50:100:10',2));assert.equal(run('state.points[0].x'),50);
line(frame(1,'1:75:125:10',4));assert.equal(run('state.map'),4);assert.equal(run('paths.length'),0);

(async()=>{
 const encoded=new TextEncoder().encode(frame(1,'2:700:400:15',4)+'\r\n'+frame(0,'',4)+'\n');
 let index=0,released=false;
 context.mockReader={async read(){if(index===encoded.length)return {done:true};return {value:encoded.slice(index,index+=Math.min(7,encoded.length-index)),done:false};},releaseLock(){released=true;}};
 run('reader=mockReader;stop=false');await run('readLoop()');
 assert(released);assert.equal(run('state.count'),0);assert.equal(run('lastPoints.size'),0);
 console.log('PASS: UART parser, ANSI, track IDs, release, invalid frames, reset, orientation and fragmented serial input.');
})().catch(error=>{console.error(error);process.exitCode=1;});

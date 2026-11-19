// Actual upload controller; only the browser/network boundary is simulated.
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const codec=require('../web/art/art-image.js');
const code=fs.readFileSync(require('node:path').join(__dirname,'../web/art/upload.js'),'utf8');
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function harness(protocol='http:'){
  const button={disabled:true,addEventListener(name,fn){this[name]=fn}},label={style:{}};
  const packet=codec.encode(new Uint8Array(96000));
  let state={storage_ready:true,has_frame:false,saved_generation:0,displayed_generation:0,display_state:'empty',refresh_wait_seconds:0};
  let postMode='ok',offline=false,posts=0,interval;
  const window={location:{protocol},ArtPrepared:{ready:()=>true,encode:()=>packet}};
  const context={window,document:{getElementById:id=>id==='upload'?button:label},AbortController,
    setTimeout:()=>1,clearTimeout(){},setInterval(fn){interval=fn},
    async fetch(url,options){
      if(offline)throw new Error('network disconnected');
      if(url==='/api/status')return {ok:true,json:async()=>({...state})};
      assert.equal(url,'/api/image');assert.equal(options.method,'POST');
      assert.equal(options.headers['X-Art-Upload'],'1');assert.equal(options.headers['Content-Type'],'application/octet-stream');
      assert.deepEqual(codec.decode(options.body).pixels,new Uint8Array(96000));++posts;
      if(postMode==='network')throw new Error('connection lost after request');
      if(postMode==='busy')return {ok:false,status:409,json:async()=>({error:'busy'})};
      if(postMode==='unconfirmed')return {ok:true,json:async()=>({saved:false})};
      state={...state,has_frame:true,saved_generation:state.saved_generation+1,display_state:'queued',refresh_wait_seconds:180};
      return {ok:true,json:async()=>({saved:true,generation:state.saved_generation})};
    }};
  vm.runInNewContext(code,context);
  return {button,label,window,get posts(){return posts},set mode(v){postMode=v},set offline(v){offline=v},
    update(values){Object.assign(state,values)},async poll(){await interval();await tick()}};
}
(async()=>{
  const h=harness();await tick();h.window.ArtUpload.prepared();assert(!h.button.disabled);
  await h.button.click();assert.equal(h.posts,1);assert(h.label.textContent.includes('сохранена')&&h.label.textContent.includes('180'));
  assert(!h.label.textContent.includes('передана на экран'));
  h.update({display_state:'displaying'});await h.poll();assert(h.button.disabled&&h.label.textContent.includes('Обновляем'));
  h.update({display_state:'displayed',displayed_generation:1});await h.poll();assert(!h.button.disabled&&h.label.textContent.includes('сохранена и передана'));
  h.mode='busy';await h.button.click();assert(h.label.textContent.includes('не подтверждена')&&h.label.textContent.includes('Повтори'));
  h.mode='unconfirmed';await h.button.click();assert(h.label.textContent.includes('не подтвердило сохранение'));
  h.mode='network';await h.button.click();assert(h.label.textContent.includes('не подтверждена'));
  h.mode='ok';await h.button.click();assert(h.label.textContent.includes('сохранена')&&!h.label.textContent.includes('не подтверждена'));
  h.update({display_state:'error',display_error:'ESP_ERR_TIMEOUT'});await h.poll();assert(h.label.textContent.includes('ошибкой (ESP_ERR_TIMEOUT)'));
  h.update({saved_generation:3,display_state:'queued'});await h.poll();assert(h.label.textContent.includes('другая картина'));
  h.offline=true;await h.poll();assert(h.button.disabled);
  const local=harness('file:');await tick();local.window.ArtUpload.prepared();assert(local.button.disabled&&local.label.textContent.includes('192.168.4.1'));
  console.log('PASS: actual upload controller: packet, commit acknowledgement, queued/displayed states, busy, lost connection, unconfirmed save, display error, replacement and local-file mode.');
})().catch(error=>{console.error(error);process.exitCode=1});

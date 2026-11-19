const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const art=require('../web/art/art-image.js');
assert.equal(art.crc32(Buffer.from('123456789')),0xcbf43926);
const rgba=new Uint8ClampedArray(800*480*4);
for(let i=0;i<800*480;i++){const v=(i%4)*85;rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=v;rgba[i*4+3]=255;}
const pixels=art.quantize(rgba,{dither:false});
assert.equal(pixels.length,96000);assert(pixels.every(x=>x===0x1b));
const preview=art.preview(pixels);assert.deepEqual(preview,rgba);
const packet=art.encode(pixels),decoded=art.decode(packet);
assert.equal(packet.length,96064);assert.deepEqual(decoded.pixels,pixels);
const reversed=art.quantize(rgba,{brightness:100,dither:false});assert(reversed.every(x=>x===255));
rgba.fill(0);assert(art.quantize(rgba,{dither:false}).every(x=>x===255)); // Transparency composites over white.
for(let i=0;i<800*480;i++){rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=127;rgba[i*4+3]=255;}
const dither=art.quantize(rgba,{dither:true}),levels=new Set(art.preview(dither).filter((_,i)=>i%4===0));
assert(levels.size>=2 && [...levels].every(x=>[0,85,170,255].includes(x)));
assert.deepEqual(dither,art.quantize(rgba,{dither:true}));
assert.throws(()=>art.quantize(rgba,{brightness:NaN}));assert.throws(()=>art.quantize(rgba,{contrast:101}));
assert.throws(()=>art.decode(packet.subarray(1)));assert.throws(()=>art.decode(new Uint8Array(96065)));
const bad=packet.slice();bad[400]^=1;assert.throws(()=>art.decode(bad),/изображение/);
const badHeader=packet.slice();badHeader[24]^=1;assert.throws(()=>art.decode(badHeader),/заголовок/);
const fixture=path.join(__dirname,'../build/host-review/art-client.ehart');fs.mkdirSync(path.dirname(fixture),{recursive:true});fs.writeFileSync(fixture,packet);
console.log('PASS: exact four levels, MSB packing, alpha, dithering, CRC and malformed client packets. Fixture: '+fixture);

// Run the actual page controller with a simulated browser boundary. Pixel
// processing is real; image decoding/canvas drawing are replaced by test input.
const vm=require('node:vm'),html=fs.readFileSync(path.join(__dirname,'../web/art/index.html'),'utf8');
const nodes=new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(m=>[m[1],{value:'0',checked:false,disabled:false,hidden:false,style:{},files:[],handlers:{},addEventListener(e,f){this.handlers[e]=f}}]));
nodes.get('fit').value='cover';nodes.get('dither').checked=true;
let drawn,downloaded;
const targetCtx={putImageData(data){assert.equal(data.width,800);assert.equal(data.height,480)}};
const sourceCtx={fillRect(){},drawImage(image,x,y,w,h){drawn={x,y,w,h}},getImageData(){return {data:rgba}}};
nodes.get('preview').getContext=()=>targetCtx;
nodes.get('preview').toBlob=f=>f(new Blob(['test-only PNG stub']));
const document={getElementById(id){assert(nodes.has(id),id);return nodes.get(id)},createElement(tag){if(tag==='canvas')return {getContext:()=>sourceCtx};if(tag==='a')return {click(){downloaded=this.download}};throw new Error(tag)}};
class MockImage{constructor(){this.naturalWidth=1200;this.naturalHeight=800}async decode(){}}
class MockImageData{constructor(data,width,height){Object.assign(this,{data,width,height})}}
vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../web/art/app.js'),'utf8'),{document,window:{ArtImage:art},Image:MockImage,ImageData:MockImageData,Blob,URL:{createObjectURL(){return 'blob:test'},revokeObjectURL(){}},requestAnimationFrame:f=>f(),setTimeout:f=>f()});
(async()=>{
  const file={name:'landscape.png',size:1024,slice(){return {async arrayBuffer(){return Uint8Array.from([137,80,78,71,13,10,26,10]).buffer}}}};
  nodes.get('file').files=[file];await nodes.get('file').handlers.change();
  assert.equal(nodes.get('export').disabled,false);assert.equal(nodes.get('empty').hidden,true);
  assert.equal(drawn.w,800);assert(Math.abs(drawn.h-533.333333)<.001);assert(Math.abs(drawn.y+26.666666)<.001);
  nodes.get('fit').value='contain';nodes.get('fit').handlers.change();
  assert.equal(drawn.w,720);assert.equal(drawn.h,480);assert.equal(drawn.x,40);assert.equal(drawn.y,0);
  assert(nodes.get('x').disabled&&nodes.get('y').disabled);
  nodes.get('export').handlers.click();assert.equal(downloaded,'landscape.ehart');
  nodes.get('brightness').value='20';nodes.get('brightness').handlers.input();assert.equal(nodes.get('brightness-value').value,'20');
  nodes.get('reset').handlers.click();assert.equal(nodes.get('brightness').value,0);assert.equal(nodes.get('fit').value,'cover');
  nodes.get('file').files=[{...file,slice(){return {async arrayBuffer(){return new Uint8Array(8).buffer}}}}];await nodes.get('file').handlers.change();
  assert(nodes.get('export').disabled&&nodes.get('png').disabled);assert(nodes.get('status').textContent.includes('PNG или JPEG'));
  console.log('PASS: real page controller, cover/contain geometry, controls, export and invalid file state (simulated browser boundary).');
})().catch(error=>{console.error(error);process.exitCode=1});

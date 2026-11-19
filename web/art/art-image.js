/* Shared browser/Node client codec. No dependencies, network calls or uploads. */
(function(root,factory){
  const api=factory();
  if(typeof module==='object'&&module.exports)module.exports=api;
  else root.ArtImage=api;
})(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const WIDTH=800,HEIGHT=480,HEADER_BYTES=64,PIXEL_BYTES=96000,PACKET_BYTES=96064;
  const MAGIC=[69,73,78,75,65,82,84,0];
  function crc32(bytes){
    let state=0xffffffff;
    for(const b of bytes){state^=b;for(let n=0;n<8;n++)state=(state>>>1)^(0xedb88320&-(state&1));}
    return (state^0xffffffff)>>>0;
  }
  function checkPixels(pixels){if(!(pixels instanceof Uint8Array)||pixels.length!==PIXEL_BYTES)throw new Error('Нужен кадр из 96 000 байт');}
  function quantize(rgba,options={}){
    if(!rgba||rgba.length!==WIDTH*HEIGHT*4)throw new Error('Нужны RGBA-пиксели 800×480');
    const brightness=options.brightness??0,contrast=options.contrast??0,dither=options.dither??true;
    if(!Number.isFinite(brightness)||Math.abs(brightness)>100||!Number.isFinite(contrast)||Math.abs(contrast)>100||typeof dither!=='boolean')throw new Error('Некорректные параметры изображения');
    const output=new Uint8Array(PIXEL_BYTES),factor=2**(contrast/50),offset=brightness*255/100;
    let row=new Float32Array(WIDTH+2),next=new Float32Array(WIDTH+2);
    for(let y=0;y<HEIGHT;y++){
      for(let x=0;x<WIDTH;x++){
        const at=(y*WIDTH+x)*4,alpha=rgba[at+3]/255;
        const luminance=(rgba[at]*.2126+rgba[at+1]*.7152+rgba[at+2]*.0722)*alpha+255*(1-alpha);
        const adjusted=(luminance-127.5)*factor+127.5+offset+(dither?row[x+1]:0);
        const value=Math.max(0,Math.min(255,adjusted)),level=Math.round(value/85);
        output[y*(WIDTH/4)+(x>>>2)]|=level<<(6-2*(x&3));
        if(dither){const error=value-level*85;row[x+2]+=error*7/16;next[x]+=error*3/16;next[x+1]+=error*5/16;next[x+2]+=error/16;}
      }
      const old=row;row=next;next=old;next.fill(0);
    }
    return output;
  }
  function preview(pixels){
    checkPixels(pixels);
    const rgba=new Uint8ClampedArray(WIDTH*HEIGHT*4);
    for(let n=0;n<WIDTH*HEIGHT;n++){
      const gray=((pixels[n>>>2]>>>(6-2*(n&3)))&3)*85;
      rgba[n*4]=rgba[n*4+1]=rgba[n*4+2]=gray;rgba[n*4+3]=255;
    }
    return rgba;
  }
  function encode(pixels){
    checkPixels(pixels);
    const packet=new Uint8Array(PACKET_BYTES),view=new DataView(packet.buffer);
    packet.set(MAGIC);view.setUint16(8,1,true);view.setUint16(10,HEADER_BYTES,true);
    view.setUint16(12,WIDTH,true);view.setUint16(14,HEIGHT,true);packet[16]=1;
    view.setUint32(20,PIXEL_BYTES,true);packet.set(pixels,HEADER_BYTES);
    view.setUint32(24,crc32(pixels),true);view.setUint32(28,crc32(packet.subarray(0,HEADER_BYTES)),true);
    return packet;
  }
  function decode(packet){
    if(!(packet instanceof Uint8Array)||packet.length!==PACKET_BYTES)throw new Error('Неверная длина файла кадра');
    const v=new DataView(packet.buffer,packet.byteOffset,packet.byteLength);
    if(!MAGIC.every((b,i)=>packet[i]===b))throw new Error('Неверная сигнатура кадра');
    if(v.getUint16(8,true)!==1||v.getUint16(10,true)!==HEADER_BYTES)throw new Error('Неизвестная версия кадра');
    if(v.getUint16(12,true)!==WIDTH||v.getUint16(14,true)!==HEIGHT)throw new Error('Нужен кадр 800×480');
    if(packet[16]!==1||packet[17]||v.getUint16(18,true)||v.getUint32(20,true)!==PIXEL_BYTES||packet.subarray(32,64).some(b=>b))throw new Error('Неизвестный формат пикселей');
    const header=packet.slice(0,HEADER_BYTES);header.fill(0,28,32);
    if(crc32(header)!==v.getUint32(28,true))throw new Error('Повреждён заголовок кадра');
    const pixels=packet.subarray(HEADER_BYTES);
    if(crc32(pixels)!==v.getUint32(24,true))throw new Error('Повреждено изображение');
    return {width:WIDTH,height:HEIGHT,pixels,pixelCRC:v.getUint32(24,true)};
  }
  return Object.freeze({WIDTH,HEIGHT,HEADER_BYTES,PIXEL_BYTES,PACKET_BYTES,crc32,quantize,preview,encode,decode});
});

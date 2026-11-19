(function(){
  'use strict';
  const $=id=>document.getElementById(id),api=window.ArtImage;
  const canvas=$('preview'),ctx=canvas.getContext('2d'),source=document.createElement('canvas');
  source.width=api.WIDTH;source.height=api.HEIGHT;
  const sourceCtx=source.getContext('2d',{willReadFrequently:true});
  let image=null,pixels=null,filename='',loadToken=0,scheduled=false;
  window.ArtPrepared=Object.freeze({ready:()=>!!pixels,encode:()=>pixels?api.encode(pixels):null});
  function preparedChanged(){if(window.ArtUpload)window.ArtUpload.prepared();}
  function status(message,error=false){$('status').textContent=message;$('status').style.color=error?'#9d2424':'';}
  function update(){
    scheduled=false;
    if(!image)return;
    try{
      const fit=$('fit').value,scale=fit==='cover'?Math.max(api.WIDTH/image.naturalWidth,api.HEIGHT/image.naturalHeight):Math.min(api.WIDTH/image.naturalWidth,api.HEIGHT/image.naturalHeight);
      const w=image.naturalWidth*scale,h=image.naturalHeight*scale;
      const x=(api.WIDTH-w)*(fit==='cover'?(Number($('x').value)+100)/200:.5),y=(api.HEIGHT-h)*(fit==='cover'?(Number($('y').value)+100)/200:.5);
      sourceCtx.fillStyle='white';sourceCtx.fillRect(0,0,api.WIDTH,api.HEIGHT);sourceCtx.imageSmoothingEnabled=true;sourceCtx.imageSmoothingQuality='high';sourceCtx.drawImage(image,x,y,w,h);
      pixels=api.quantize(sourceCtx.getImageData(0,0,api.WIDTH,api.HEIGHT).data,{brightness:Number($('brightness').value),contrast:Number($('contrast').value),dither:$('dither').checked});
      ctx.putImageData(new ImageData(api.preview(pixels),api.WIDTH,api.HEIGHT),0,0);
      $('empty').hidden=true;$('export').disabled=$('png').disabled=false;
      $('x').disabled=$('y').disabled=fit!=='cover';
      status('Готово · 800 × 480 · 4 уровня серого · 96 000 байт изображения');
      preparedChanged();
    }catch(error){pixels=null;$('export').disabled=$('png').disabled=true;preparedChanged();status('Не удалось подготовить картину: '+error.message,true);}
  }
  function schedule(){if(!scheduled){scheduled=true;requestAnimationFrame(update);}}
  for(const id of ['x','y','brightness','contrast'])$(id).addEventListener('input',()=>{$(id+'-value').value=$(id).value;schedule();});
  for(const id of ['fit','dither'])$(id).addEventListener('change',schedule);
  function reset(){for(const id of ['x','y','brightness','contrast']){$(id).value=0;$(id+'-value').value='0';}$('fit').value='cover';$('dither').checked=true;}
  $('reset').addEventListener('click',()=>{reset();schedule();});
  $('file').addEventListener('change',async()=>{
    const file=$('file').files[0],token=++loadToken;
    if(!file)return;
    image=null;pixels=null;$('adjustments').disabled=true;$('export').disabled=$('png').disabled=true;
    preparedChanged();
    status('Открываем изображение…');
    let url;
    try{
      if(file.size>50*1024*1024)throw new Error('файл больше 50 МБ; выбери уменьшенную копию');
      const signature=new Uint8Array(await file.slice(0,8).arrayBuffer());
      const png=[137,80,78,71,13,10,26,10].every((b,i)=>signature[i]===b),jpeg=signature[0]===255&&signature[1]===216&&signature[2]===255;
      if(!png&&!jpeg)throw new Error('выбери PNG или JPEG');
      url=URL.createObjectURL(file);
      const decoded=new Image();decoded.src=url;await decoded.decode();
      if(token!==loadToken)return;
      if(!decoded.naturalWidth||!decoded.naturalHeight||decoded.naturalWidth*decoded.naturalHeight>64000000)throw new Error('нужно изображение не больше 64 мегапикселей');
      image=decoded;filename=file.name.replace(/\.[^.]+$/,'')||'picture';
      $('filename').textContent=`${file.name} · ${decoded.naturalWidth} × ${decoded.naturalHeight}`;
      $('adjustments').disabled=false;reset();update();
    }catch(error){if(token===loadToken)status('Не удалось открыть файл: '+error.message,true);}
    finally{if(url)URL.revokeObjectURL(url);}
  });
  function download(blob,suffix){const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=filename+suffix;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  $('export').addEventListener('click',()=>{if(pixels)download(new Blob([api.encode(pixels)],{type:'application/octet-stream'}),'.ehart');});
  $('png').addEventListener('click',()=>{if(pixels)canvas.toBlob(blob=>{if(blob)download(blob,'-epaper.png');},'image/png');});
})();

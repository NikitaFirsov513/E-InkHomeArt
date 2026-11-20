(function(){
  'use strict';
  const button=document.getElementById('upload'),label=document.getElementById('device-status');
  let device=null,sending=false,prepared=false,wanted=null,polling=false,uploadError='';
  const local=window.location.protocol==='http:'||window.location.protocol==='https:';
  function message(text,error=false){label.textContent=text;label.style.color=error?'#9d2424':'';}
  function controls(){
    button.disabled=!prepared||!device||!device.storage_ready||device.display_state==='displaying'||sending;
    button.textContent=sending?'Отправляем и сохраняем…':'Показать на устройстве';
  }
  window.ArtUpload=Object.freeze({prepared(){prepared=window.ArtPrepared.ready();controls();}});
  function showState(){
    controls();
    if(sending){message('Передаём готовый кадр и проверяем запись на устройстве…');return;}
    if(uploadError){message(uploadError,true);return;}
    if(!device){message('Нет связи с панелью. Проверь подключение к её Wi-Fi и открой http://192.168.4.1.',true);return;}
    if(!device.storage_ready){message('Хранилище недоступно. Проверь журнал UART устройства.',true);return;}
    if(wanted!==null&&device.saved_generation!==wanted){message('На устройстве уже выбрана другая картина. Можно отправить эту снова.');return;}
    if(device.display_state==='error'){
      if(device.target_mode==='home'){message('Не удалось обновить меню панели ('+device.display_error+'). Картина во Flash сохранена.',true);return;}
      message('Картина сохранена, но обновление экрана завершилось ошибкой ('+device.display_error+'). Можно отправить её повторно.',true);return;
    }
    if(device.display_state==='displaying'){message(device.target_mode==='home'?'Открываем меню панели…':device.has_frame?'Картина сохранена. Обновляем экран…':'Готовим экран загрузки…');return;}
    if(device.display_state==='queued'){message(device.target_mode==='home'?'Открываем меню панели…':'Картина сохранена. Начинаем обновление экрана…');return;}
    if(device.screen_mode==='home'){message('На панели открыт демонстрационный интерфейс. Отправка фото вернёт картину на экран.');return;}
    if(device.has_frame&&device.display_state==='displayed'&&device.displayed_generation===device.saved_generation){
      message('Картина сохранена и передана на экран. Она останется на устройстве после отключения питания.');return;
    }
    message('Устройство подключено. Выбери изображение и нажми «Показать на устройстве».');
  }
  async function poll(){
    if(polling||sending||!local)return;
    polling=true;
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),5000);
    try{
      const response=await fetch('/api/status',{cache:'no-store',signal:controller.signal});
      if(!response.ok)throw new Error('HTTP '+response.status);
      const data=await response.json();
      if(typeof data.storage_ready!=='boolean'||typeof data.display_state!=='string')throw new Error('Unknown device');
      device=data;
    }catch(error){device=null;}
    finally{clearTimeout(timer);polling=false;showState();}
  }
  button.addEventListener('click',async()=>{
    if(button.disabled)return;
    const packet=window.ArtPrepared.encode();
    if(!packet)return;
    sending=true;uploadError='';showState();
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),40000);
    try{
      const response=await fetch('/api/image',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-Art-Upload':'1'},body:packet,signal:controller.signal});
      const data=await response.json();
      if(!response.ok)throw new Error(response.status===409?'Экран сейчас обновляется. Повтори отправку после завершения.':data.error||'HTTP '+response.status);
      if(data.saved!==true||!Number.isInteger(data.generation))throw new Error('Устройство не подтвердило сохранение');
      wanted=data.generation;
    }catch(error){
      uploadError='Отправка не подтверждена: '+(error.name==='AbortError'?'истекло время ожидания':error.message)+'. Проверь состояние панели перед повтором.';
    }finally{
      clearTimeout(timer);sending=false;await poll();
    }
  });
  if(local){poll();setInterval(poll,1000);}
  else message('Для отправки подключись к Wi-Fi устройства и открой http://192.168.4.1. Здесь можно подготовить и скачать кадр.');
})();

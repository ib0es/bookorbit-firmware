#include "web_config.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <vector>

#include "app/app_state.h"
#include "../core/book_formats.h"
#include "../core/time_utils.h"
#include "../core/template_stream.h"
#include "../core/ui_port.h"
#include "../platform/device.h"
#include "../platform/system.h"
#include "../platform/hal/mem_stats.h"
#include "platform/hal/buttons.h"
#include "platform/hal/display.h"
#include "platform/hal/frontlight.h"
#include "platform/hal/imu.h"
#include "platform/hal/storage.h"
#include "bookorbit_client.h"
#include "../reader/book_reader.h"
#include "../reader/font_store.h"
#include "../reader/reader.h"
#include "services/library_manager.h"
#include "services/power_service.h"
#include "services/wifi_session.h"
#include "ui/font.h"
#include "ui/i18n.h"
#include "ui/text_utils.h"

WebConfigManager WebConfig;

WebConfigManager::WebConfigManager() {}

namespace {

const char HTML_HEAD[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta charset='utf-8'>
<title>%HTML_TITLE%</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f4f6f8; margin: 0; padding: 14px; color: #222; }
.card { background: #fff; max-width: 520px; margin: 0 auto; border-radius: 12px; padding: 18px; box-shadow: 0 4px 16px rgba(0,0,0,0.08); }
h1 { font-size: 20px; margin: 0 0 4px; color: #111; }
h2 { font-size: 16px; margin: 14px 0 8px; color: #111; }
p.desc { font-size: 13px; color: #666; margin: 0 0 12px; line-height: 1.4; }
.tabs { display: flex; gap: 6px; margin-bottom: 14px; border-bottom: 1px solid #e5e7eb; padding-bottom: 8px; }
.tab { flex: 1; background: #eef1f4; color: #222; border: none; padding: 10px 8px; font-size: 14px; font-weight: 600; border-radius: 8px; cursor: pointer; margin: 0; }
.tab.active { background: #000; color: #fff; }
.panel { display: none; }
.panel.active { display: block; }
.sd-bar { font-size: 12px; color: #444; background: #f3f4f6; padding: 8px 10px; border-radius: 6px; margin-bottom: 10px; }
.drop { border: 2px dashed #bbb; border-radius: 8px; padding: 14px; text-align: center; background: #fafafa; margin-bottom: 10px; }
.drop input[type='file'] { width: 100%; font-size: 14px; }
.qitem { display: flex; justify-content: space-between; align-items: center; padding: 6px 0; border-bottom: 1px solid #eee; font-size: 13px; gap: 8px; }
.qname { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; flex: 1; }
.badge { font-size: 12px; font-weight: 600; padding: 2px 6px; border-radius: 4px; background: #e5e7eb; }
.b-ok { background: #dcfce7; color: #166534; }
.b-err { background: #fee2e2; color: #991b1b; }
.b-skip { background: #fef3c7; color: #92400e; }
progress { width: 100%; height: 10px; margin: 8px 0; }
.blist { max-height: 280px; overflow-y: auto; border: 1px solid #e5e7eb; border-radius: 8px; padding: 4px 10px; }
.bdel { width: auto; padding: 5px 10px; font-size: 12px; background: #dc2626; margin: 0; border-radius: 6px; }
.toast { display: none; padding: 10px 12px; border-radius: 8px; margin-bottom: 10px; font-size: 13px; font-weight: 600; }
.toast.ok { display: block; background: #dcfce7; color: #166534; }
.toast.err { display: block; background: #fee2e2; color: #991b1b; }
details { border: 1px solid #e5e7eb; border-radius: 8px; padding: 10px 14px; margin-bottom: 12px; background: #fafafa; }
summary { font-size: 15px; font-weight: 700; color: #111; cursor: pointer; list-style: none; display: flex; justify-content: space-between; align-items: center; }
summary::-webkit-details-marker { display: none; }
summary::after { content: '▾'; font-size: 13px; color: #666; }
details:not([open]) summary::after { content: '▸'; }
.sec-body { margin-top: 12px; }
.fg { margin-bottom: 12px; }
.fg:last-child { margin-bottom: 2px; }
label.lbl { display: block; font-size: 13px; font-weight: 600; margin-bottom: 5px; color: #444; }
input[type='text'], input[type='password'], select { width: 100%; box-sizing: border-box; padding: 10px 11px; border: 1px solid #ccc; border-radius: 6px; font-size: 15px; background: #fff; color: #111; }
input:focus, select:focus { border-color: #000; outline: none; }
label.chk { display: flex; align-items: center; font-size: 14px; font-weight: 400; color: #222; cursor: pointer; margin-bottom: 10px; }
label.chk input { width: 18px; height: 18px; margin: 0 10px 0 0; flex-shrink: 0; }
button.btn { width: 100%; background: #000; color: #fff; border: none; padding: 13px; font-size: 15px; font-weight: 600; border-radius: 8px; cursor: pointer; margin-top: 6px; }
button.btn:disabled { opacity: 0.45; cursor: default; }
button.btn-sec { background: #4b5563; margin-top: 10px; }
.footer { text-align: center; font-size: 12px; color: #888; margin-top: 14px; }
</style>
</head>
<body>
<div class='card'>
<h1>%HTML_HEADING%</h1>
<p class='desc'>%HTML_SUBHEAD%</p>
<div id='toast' class='toast'></div>
<div class='tabs'>
<button type='button' class='tab active' onclick='showTab("books",this)'>%TAB_BOOKS%</button>
<button type='button' class='tab' onclick='showTab("fonts",this)'>%FONTS_TITLE%</button>
<button type='button' class='tab' onclick='showTab("settings",this)'>%TAB_SETTINGS%</button>
</div>
<div id='tab-books' class='panel active'>
<div class='sd-bar'>%SD_FREE_LBL%: <span id='sd-info'>...</span></div>
<h2>%UPLOAD_TITLE%</h2>
<p class='desc'>%UPLOAD_DESC%</p>
<div class='drop'><input type='file' id='file-input' multiple accept='.epub,.fb2,.mobi,.azw3,.azw,.txt' title='%PICK_FILES%'></div>
<label class='chk'><input type='checkbox' id='overwrite'>%OVERWRITE_LBL%</label>
<button type='button' id='btn-up' class='btn' onclick='startUpload()' disabled>%BTN_UPLOAD%</button>
<div id='prog-wrap' style='display:none'>
<progress id='p-batch' value='0' max='100'></progress>
<div id='p-label' class='desc' style='margin:0 0 6px'></div>
</div>
<div id='queue'></div>
<h2>%BOOKS_LIST_TITLE% (<span id='book-cnt'>0</span>)</h2>
<div id='blist' class='blist'></div>
</div>
<div id='tab-fonts' class='panel'>
<h2>%FONT_SD_TITLE%</h2>
<p class='desc'>%FONTS_DESC%</p>
<div class='drop'><input type='file' id='font-input' multiple accept='.zip,.ttf,.otf'></div>
<label class='chk'><input type='checkbox' id='font-overwrite'>%OVERWRITE_LBL%</label>
<button type='button' id='font-up' class='btn' onclick='uploadFonts()' disabled>%FONT_SD_UPLOAD%</button>
<progress id='font-progress' value='0' max='100' style='display:none'></progress>
<div id='font-result' class='desc' role='status'></div>
<div id='font-list' class='blist'></div>
<h2>%FONT_FLASH_TITLE%</h2>
<p class='desc'>%FONT_FLASH_HINT%</p>
<p id='font-flash-capacity' class='desc'></p>
<select id='font-flash-family' onchange='updateFontAction()'></select>
<button type='button' id='font-flash-install' class='btn' onclick='runFontAction()' disabled>%FONT_FLASH_INSTALL%</button>
</div>
<div id='tab-settings' class='panel'>
<form id='cfg-form' method='POST' action='/save'>
)rawliteral";

const char HTML_TAIL[] PROGMEM = R"rawliteral(<button type='submit' id='btn-save' class='btn'>%BTN_SAVE%</button>
</form>
</div>
<button type='button' id='btn-close' class='btn btn-sec' onclick='closePortal()'>%BTN_CLOSE%</button>
<div class='footer'>%HTML_FOOTER%</div>
</div>
<script>
var T={noBooks:'%JS_NO_BOOKS%',del:'%JS_DELETE%',saved:'%JS_SAVED%',err:'%JS_ERR%',closed:'%JS_CLOSED%',freeLbl:'%JS_FREE%'};
var upQueue=[],uploading=false;
function $(id){return document.getElementById(id);}
function toast(msg,ok){var t=$('toast');t.textContent=msg;t.className='toast '+(ok?'ok':'err');}
function fmtSz(b){if(b>=1073741824)return(b/1073741824).toFixed(2)+' GB';if(b>=1048576)return(b/1048576).toFixed(1)+' MB';if(b>=1024)return Math.round(b/1024)+' KB';return b+' B';}
function showTab(id,btn){
  document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active');});
  document.querySelectorAll('.tab').forEach(function(b){b.classList.remove('active');});
  $('tab-'+id).classList.add('active');btn.classList.add('active');
  if(id==='fonts')loadFonts();
}
var booksLoading=false;
function loadBooks(){
  if(booksLoading)return;
  booksLoading=true;
  fetch('/api/books').then(function(r){return r.json();}).then(function(d){
    booksLoading=false;
    $('sd-info').textContent=(d.free?fmtSz(d.free):'?')+' / '+(d.total?fmtSz(d.total):'?');
    var list=$('blist');list.innerHTML='';
    var books=d.books||[];$('book-cnt').textContent=books.length;
    if(!books.length){list.innerHTML='<div class="qitem"><span>'+T.noBooks+'</span></div>';return;}
    books.forEach(function(b){
      var row=document.createElement('div');row.className='qitem';
      var nm=document.createElement('span');nm.className='qname';nm.textContent=b.name+' ('+fmtSz(b.size)+')';
      var btn=document.createElement('button');btn.type='button';btn.className='bdel';btn.textContent=T.del;
      btn.onclick=function(){delBook(b.name);};
      row.appendChild(nm);row.appendChild(btn);list.appendChild(row);
    });
  }).catch(function(){booksLoading=false;});
}
function delBook(name){
  fetch('/api/books/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+encodeURIComponent(name)})
  .then(function(r){return r.json();}).then(function(d){if(d.ok)loadBooks();else toast(d.error||T.err,false);})
  .catch(function(){toast(T.err,false);});
}
$('file-input').addEventListener('change',function(){
  if(uploading)return;
  upQueue=Array.prototype.slice.call(this.files||[]);
  renderQueue();
  $('prog-wrap').style.display='none';
  $('p-batch').value=0;
  $('p-label').textContent='';
  $('btn-up').disabled=!upQueue.length;
});
function renderQueue(states){
  var q=$('queue');q.innerHTML='';
  upQueue.forEach(function(f,i){
    var row=document.createElement('div');row.className='qitem';
    var nm=document.createElement('span');nm.className='qname';nm.textContent=f.name+' ('+fmtSz(f.size)+')';
    var st=document.createElement('span');st.id='qst-'+i;st.className='badge';
    st.textContent=(states&&states[i])?states[i]:'...';
    row.appendChild(nm);row.appendChild(st);q.appendChild(row);
  });
}
function startUpload(){
  if(uploading||fontUploading||!upQueue.length)return;
  uploading=true;$('btn-up').disabled=true;$('file-input').disabled=true;$('prog-wrap').style.display='block';
  var ow=$('overwrite').checked?'1':'0';
  var files=upQueue.slice(),total=0,ends=[],sizes=[];
  // Every file in one request: one connection for the whole batch.
  var fd=new FormData();
  files.forEach(function(f){fd.append('file',f,f.name);total+=f.size;ends.push(total);sizes.push(f.size);});
  function badge(i,txt,cls){var st=$('qst-'+i);if(st){st.textContent=txt;st.className='badge'+(cls?' '+cls:'');}}
  function finish(){
    uploading=false;upQueue=[];
    $('file-input').value='';$('file-input').disabled=false;$('btn-up').disabled=true;
    $('p-batch').value=100;
    setTimeout(loadBooks,150);
  }
  var cur=-1;
  var xhr=new XMLHttpRequest();
  xhr.timeout=0;
  xhr.open('POST','/api/upload?overwrite='+ow+'&sizes='+sizes.join(','),true);
  xhr.upload.onprogress=function(e){
    if(!e.lengthComputable||e.total<=0)return;
    var sent=e.loaded*total/e.total;
    $('p-batch').value=Math.round(e.loaded*100/e.total);
    for(var i=0;i<files.length;i++){
      var start=i?ends[i-1]:0;
      if(sent<ends[i]||i==files.length-1){
        if(i!==cur){for(var k=Math.max(cur,0);k<i;k++)badge(k,'100%');cur=i;$('p-label').textContent=(i+1)+' / '+files.length+': '+files[i].name;}
        badge(i,Math.min(100,Math.round((sent-start)*100/Math.max(files[i].size,1)))+'%');
        break;
      }
    }
  };
  xhr.onload=function(){
    var res={};try{res=JSON.parse(xhr.responseText);}catch(e){}
    var out=res.files||[];
    files.forEach(function(f,i){
      var r=out[i];
      if(!r)badge(i,'err','b-err');
      else if(r.ok)badge(i,'OK','b-ok');
      else if(r.exists)badge(i,'skip','b-skip');
      else badge(i,r.error||'err','b-err');
    });
    finish();
  };
  xhr.onerror=xhr.ontimeout=function(){files.forEach(function(f,i){badge(i,'err','b-err');});finish();};
  xhr.send(fd);
}
var flashChoices=Object.create(null);
function updateFontAction(){
  var choice=flashChoices[fontFamilyKey($('font-flash-family').value)],button=$('font-flash-install');
  button.textContent=choice&&choice.builtin?'%JS_FLASH_BUILTIN%':(choice&&choice.installed?'%JS_FLASH_DELETE%':'%JS_FLASH_INSTALL%');
  button.style.background=choice&&choice.installed?'#b91c1c':'#000';
  button.disabled=fontUploading||!choice||choice.builtin||(!choice.installed&&!choice.fits);
}
function runFontAction(){
  var choice=flashChoices[fontFamilyKey($('font-flash-family').value)];
  if(!choice||choice.builtin||fontUploading||uploading)return;
  return choice.installed?deletePortalFont():installPortalFont();
}
function loadFonts(){
  return fetch('/api/fonts').then(function(r){if(!r.ok)throw Error(T.err);return r.json();}).then(function(d){
    var list=$('font-list');list.textContent='';
    var select=$('cfg-form').elements.namedItem('font_family'),current=select.value;
    while(select.options.length>1)select.remove(1);
    var flashSelect=$('font-flash-family'),previous=flashSelect.value;flashSelect.options.length=0;
    flashSelect.add(new Option('—',''));
    var installed=d.installedFamilies||[];flashChoices=Object.create(null);
    $('font-flash-capacity').textContent='Flash: '+fmtSz(d.used||0)+' / '+fmtSz(d.capacity||0)+' · %JS_FLASH_FREE%: '+fmtSz(d.free||0);
    var names=[];
    (d.families||[]).forEach(function(f){
      names.push(f.name);flashChoices[fontFamilyKey(f.name)]=f;
      var row=document.createElement('div');row.className='qitem';row.textContent=f.name+' ('+fmtSz(f.bytes)+')';
      var remove=document.createElement('button');remove.type='button';remove.className='btn';remove.textContent='%JS_FONT_SD_DELETE%';
      remove.disabled=fontUploading||uploading;remove.onclick=function(){deletePortalCardFont(f.name);};row.appendChild(remove);list.appendChild(row);
    });
    installed.forEach(function(f){
      flashChoices[fontFamilyKey(f.name)]={name:f.name,bytes:f.bytes,installed:true,fits:true};
      if(names.indexOf(f.name)<0)names.push(f.name);
    });
    var builtinName=d.builtin||'Noto Serif';
    flashChoices[fontFamilyKey(builtinName)]={name:builtinName,bytes:0,builtin:true,installed:false,fits:false};
    Object.keys(flashChoices).forEach(function(key){
      var f=flashChoices[key];
      var option=new Option(f.builtin?f.name+' — %JS_FLASH_BUILTIN%':f.name+' ('+fmtSz(f.bytes)+')'+(f.installed?' — %JS_FONT_ALREADY%':(f.fits?'':' — %JS_FONT_TOO_BIG%')),f.name);
      option.disabled=!!f.builtin||(!f.installed&&!f.fits);flashSelect.add(option);
    });
    var previousChoice=flashChoices[fontFamilyKey(previous)];flashSelect.value=previousChoice?previousChoice.name:'';
    updateFontAction();
    names.forEach(function(name){select.add(new Option(name,name));});
    if(current&&names.indexOf(current)<0)select.add(new Option(current,current));
    select.value=current;
    if(!(d.families||[]).length)list.textContent='%JS_FONTS_EMPTY%';
  }).catch(function(){toast(T.err,false);});
}
// ZIP is expanded in the browser: the reader receives ordinary font uploads.
function fontZipError(){return Error('%JS_ZIP_ERROR%');}
function fontCrc32(bytes){
  var crc=0xffffffff;
  for(var i=0;i<bytes.length;i++){
    crc^=bytes[i];
    for(var bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);
  }
  return (crc^0xffffffff)>>>0;
}
async function unzipFonts(file){
  if(file.size>32*1024*1024)throw fontZipError();
  var data=new Uint8Array(await file.arrayBuffer()),view=new DataView(data.buffer);
  var end=-1;
  for(var at=data.length-22;at>=Math.max(0,data.length-65557);at--){
    if(view.getUint32(at,true)===0x06054b50&&at+22+view.getUint16(at+20,true)===data.length){end=at;break;}
  }
  if(end<0||view.getUint16(end+4,true)!==0||view.getUint16(end+6,true)!==0)throw fontZipError();
  var count=view.getUint16(end+10,true),pos=view.getUint32(end+16,true),dirSize=view.getUint32(end+12,true);
  if(count===65535||count>1024||count!==view.getUint16(end+8,true)||pos+dirSize>end)throw fontZipError();
  var dirEnd=pos+dirSize,fonts=[],total=0,names=new Set();
  for(var index=0;index<count;index++){
    if(pos+46>dirEnd||view.getUint32(pos,true)!==0x02014b50)throw fontZipError();
    var flags=view.getUint16(pos+8,true),method=view.getUint16(pos+10,true),crc=view.getUint32(pos+16,true);
    var compressed=view.getUint32(pos+20,true),size=view.getUint32(pos+24,true);
    var nameLen=view.getUint16(pos+28,true),extra=view.getUint16(pos+30,true),comment=view.getUint16(pos+32,true);
    var local=view.getUint32(pos+42,true);
    if(pos+46+nameLen+extra+comment>dirEnd||view.getUint16(pos+34,true)!==0)throw fontZipError();
    var name=new TextDecoder().decode(data.subarray(pos+46,pos+46+nameLen));
    pos+=46+nameLen+extra+comment;
    if(!/\.(ttf|otf)$/i.test(name))continue;
    var parts=name.split('/'),base=parts[parts.length-1],key=base.toLowerCase();
    if(parts.some(function(p){return !p||p==='.'||p==='..';})||/[\\\x00-\x1f:*?"<>|]/.test(name)||base[0]==='.'||names.has(key))throw fontZipError();
    if((flags&1)||(method!==0&&method!==8)||!size||size>16*1024*1024||fonts.length>=128)throw fontZipError();
    total+=size;if(total>64*1024*1024)throw fontZipError();
    if(local+30>data.length||view.getUint32(local,true)!==0x04034b50||view.getUint16(local+8,true)!==method)throw fontZipError();
    var start=local+30+view.getUint16(local+26,true)+view.getUint16(local+28,true);
    if(start+compressed>dirEnd||start+compressed>data.length)throw fontZipError();
    var payload=data.subarray(start,start+compressed),chunks=[],length=0;
    if(method===0){if(compressed!==size)throw fontZipError();chunks=[payload];length=payload.length;}
    else{
      var decoder;
      try{decoder=new DecompressionStream('deflate-raw');}catch(e){throw Error('%JS_ZIP_BROWSER%');}
      var reader=new Blob([payload]).stream().pipeThrough(decoder).getReader();
      try{
        while(true){var part=await reader.read();if(part.done)break;
          length+=part.value.length;if(length>size)throw fontZipError();chunks.push(part.value);
        }
      }finally{await reader.cancel().catch(function(){});}
    }
    if(length!==size)throw fontZipError();
    var font=new File(chunks,base,{type:'application/octet-stream'});
    if(fontCrc32(new Uint8Array(await font.arrayBuffer()))!==crc)throw fontZipError();
    fonts.push(font);names.add(key);
  }
  if(!fonts.length)throw Error('%JS_ZIP_EMPTY%');
  return fonts;
}
async function expandFontFiles(files){
  var result=[],names=new Set(),total=0;
  for(var i=0;i<files.length;i++){
    var list=/\.zip$/i.test(files[i].name)?await unzipFonts(files[i]):[files[i]];
    for(var j=0;j<list.length;j++){
      var f=list[j],name=f.name.toLowerCase();
      if(!/\.(ttf|otf)$/i.test(name)||names.has(name)||f.size>16*1024*1024||result.length>=128)throw fontZipError();
      total+=f.size;if(total>64*1024*1024)throw fontZipError();
      names.add(name);result.push(f);
    }
  }
  return result;
}

function fontFamilyKey(name){return name.toLowerCase().replace(/[ _-]/g,'');}
function fontFamilyFromFile(name){return name.replace(/\.(ttf|otf)$/i,'').replace(/[- ](regular|book|roman|normal|bolditalic|boldoblique|bold|italic|oblique|bi|it)$/i,'');}
async function deletePortalCardFont(name){
  if(fontUploading||uploading)return;
  fontUploading=true;$('font-up').disabled=true;$('font-flash-install').disabled=true;
  $('btn-save').disabled=true;$('btn-close').disabled=true;
  try{
    var response=await fetch('/api/fonts/card/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+encodeURIComponent(name)}),result=await response.json();
    if(!response.ok||!result.ok)throw Error(result.error||T.err);
    var select=$('cfg-form').elements.namedItem('font_family');
    if(!result.inFlash&&fontFamilyKey(select.value)===fontFamilyKey(name))select.value='';
    toast('%JS_FONT_SD_DELETED%',true);
  }catch(e){toast(e.message,false);}
  finally{
    fontUploading=false;$('font-up').disabled=!$('font-input').files.length;
    $('btn-save').disabled=false;$('btn-close').disabled=false;await loadFonts();
  }
}
async function deletePortalFont(){
  if(fontUploading||uploading)return;
  fontUploading=true;$('font-flash-install').disabled=true;$('btn-save').disabled=true;$('btn-close').disabled=true;
  try{
    var name=$('font-flash-family').value;
    var response=await fetch('/api/fonts/flash/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+encodeURIComponent(name)}),result=await response.json();
    if(!response.ok||!result.ok)throw Error(result.error||T.err);
    var familySelect=$('cfg-form').elements.namedItem('font_family');if(familySelect.value===name)familySelect.value='';toast('%JS_FONT_DELETED%',true);
  }catch(e){toast(e.message,false);}
  finally{fontUploading=false;$('btn-save').disabled=false;$('btn-close').disabled=false;loadFonts();}
}
async function installPortalFont(){
  if(fontUploading||uploading)return;
  var name=$('font-flash-family').value;if(!name)return;
  fontUploading=true;$('font-flash-install').disabled=true;$('font-up').disabled=true;
  $('btn-save').disabled=true;$('btn-close').disabled=true;
  try{
    var response=await fetch('/api/fonts/install',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+encodeURIComponent(name)});
    var result=await response.json();if(!response.ok||!result.ok)throw Error(result.error||T.err);
    $('cfg-form').elements.namedItem('font_family').value=name;
    toast('%JS_FONT_FLASH_DONE%',true);
  }catch(e){toast(e.message,false);}
  finally{
    fontUploading=false;$('font-up').disabled=!$('font-input').files.length;
    $('btn-save').disabled=false;$('btn-close').disabled=false;loadFonts();
  }
}
var fontUploading=false;
$('font-input').addEventListener('change',function(){$('font-up').disabled=fontUploading||!this.files.length;});
async function uploadFonts(){
  if(fontUploading||uploading)return;
  var files=Array.prototype.slice.call($('font-input').files||[]);if(!files.length)return;
  var overwrite=$('font-overwrite').checked;
  fontUploading=true;$('font-up').disabled=true;$('font-input').disabled=true;$('btn-up').disabled=true;
  $('btn-save').disabled=true;$('btn-close').disabled=true;$('font-progress').style.display='block';
  $('font-result').textContent='';
  try{
    $('font-result').textContent='%JS_ZIP_PREPARING%';
    files=await expandFontFiles(files);
    var stateResponse=await fetch('/api/fonts');if(!stateResponse.ok)throw Error(T.err);
    var state=await stateResponse.json();
    files=files.filter(function(f){return fontFamilyKey(fontFamilyFromFile(f.name))!==fontFamilyKey(state.builtin||'Noto Serif');});
    if(files.some(function(f){return (state.installedFamilies||[]).some(function(installed){return fontFamilyKey(fontFamilyFromFile(f.name))===fontFamilyKey(installed.name);});}))throw Error('%JS_FONT_DELETE_FIRST%');
    if(!files.length)throw Error('%JS_FONT_BUILTIN_ONLY%');
    $('font-result').textContent='';
    for(var i=0;i<files.length;i++){
      var file=files[i];
      await new Promise(function(resolve,reject){
        var fd=new FormData();fd.append('file',file,file.name);
        var xhr=new XMLHttpRequest();
        xhr.open('POST','/api/upload?kind=font&overwrite='+(overwrite?'1':'0')+'&sizes='+file.size,true);
        xhr.upload.onprogress=function(e){if(e.lengthComputable)$('font-progress').value=(i+e.loaded/e.total)*100/files.length;};
        xhr.onload=function(){
          try{var d=JSON.parse(xhr.responseText),r=(d.files||[])[0];
            if(xhr.status===200&&r&&r.exists&&!overwrite){
              $('font-result').textContent+=file.name+': %JS_FONT_EXISTS%\n';resolve();return;
            }
            if(xhr.status!==200||!r||!r.ok)throw Error(r&&r.error||T.err);
            $('font-result').textContent+=(file.name+': OK\n');resolve();
          }catch(e){reject(e);}
        };
        xhr.onerror=xhr.ontimeout=xhr.onabort=function(){reject(Error(T.err));};xhr.send(fd);
      });
    }
    $('font-progress').value=100;toast('%JS_FONTS_DONE%',true);
  }catch(e){toast((file?file.name+': ':'')+e.message,false);}
  finally{
    fontUploading=false;$('font-input').disabled=false;$('font-up').disabled=!$('font-input').files.length;
    $('btn-up').disabled=!upQueue.length;$('btn-save').disabled=false;$('btn-close').disabled=false;
    loadFonts();
  }
}
$('cfg-form').addEventListener('submit',function(ev){
  ev.preventDefault();
  var body=new URLSearchParams(new FormData(this)).toString();
  fetch('/save?ajax=1',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(function(r){return r.json();}).then(function(d){toast(d.ok?T.saved:(d.error||T.err),!!d.ok);})
  .catch(function(){toast(T.err,false);});
});
function closePortal(){
  fetch('/api/close',{method:'POST'}).then(function(){toast(T.closed,true);}).catch(function(){toast(T.closed,true);});
}
loadBooks();
</script>
</body>
</html>
)rawliteral";

const char HTML_SAVED[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta charset='utf-8'>
<title>%SAVED_TITLE%</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f4f6f8; margin: 0; padding: 24px; color: #222; text-align: center; }
.card { background: #fff; max-width: 400px; margin: 40px auto; border-radius: 12px; padding: 32px 24px; box-shadow: 0 4px 16px rgba(0,0,0,0.08); }
h1 { font-size: 22px; color: #15803d; margin-top: 0; }
p { font-size: 15px; color: #555; line-height: 1.5; }
a { display: inline-block; margin-top: 12px; color: #111; font-weight: 600; }
</style>
</head>
<body>
<div class='card'>
<h1>✓ %SAVED_TITLE%</h1>
<p>%SAVED_DESC%</p>
<p>%SAVED_HINT%</p>
<a href='/'>&larr; %TAB_BOOKS%</a>
</div>
</body>
</html>
)rawliteral";

// Values put into the form: a quote or '<' in a name must not break the page.
String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        const char c = s[i];
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '\'':
                out += "&#39;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

String jsEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        const char c = s[i];
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\'':
                out += "\\'";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '<':
                out += "\\x3c";
                break;
            case '>':
                out += "\\x3e";
                break;
            default:
                out += c;
        }
    }
    return out;
}

String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        const uint8_t c = (uint8_t)s[i];
        if (c == '\\')
            out += "\\\\";
        else if (c == '"')
            out += "\\\"";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c < 0x20)
            out += ' ';
        else
            out += (char)c;
    }
    return out;
}

bool sanitizeUploadFilename(const String& raw, String& outName, bool font = false) {
    int slash = raw.lastIndexOf('/');
    int bslash = raw.lastIndexOf('\\');
    int start = (slash > bslash ? slash : bslash) + 1;
    String base = raw.substring(start);
    base.trim();
    if (base.length() == 0 || base.startsWith(".")) return false;

    String clean;
    clean.reserve(base.length());
    for (size_t i = 0; i < base.length(); i++) {
        const uint8_t c = (uint8_t)base[i];
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            clean += '_';
        } else {
            clean += (char)c;
        }
    }
    while (clean.endsWith(".") || clean.endsWith(" ")) clean.remove(clean.length() - 1);
    clean.trim();
    if (clean.length() == 0 || clean == "." || clean == ".." || clean.startsWith(".")) return false;

    const BookFormats::Kind kind = BookFormats::fromPath(clean.c_str());
    String lower = clean;
    lower.toLowerCase();
    if (font ? !(lower.endsWith(".ttf") || lower.endsWith(".otf")) : kind == BookFormats::Unknown) return false;

    const int dot = clean.lastIndexOf('.');
    const String ext = (dot > 0) ? clean.substring(dot) : String();
    String stem = (dot > 0) ? clean.substring(0, dot) : clean;
    stem.trim();
    if (stem.length() == 0) return false;
    if (stem.length() > 110) stem = utf8Prefix(stem, 110).c_str();
    outName = stem + ext;
    return true;
}

struct PortalSink {
    WebServer& server;
    void operator()(const char* bytes, size_t length) const {
        server.sendContent(bytes, length);
        Platform::feedWatchdog();
        PowerService::noteActivity();
        delay(0);
    }
};
class PortalWriter {
public:
    explicit PortalWriter(WebServer& server) : buffer_(PortalSink{server}) {}
    void append(const char* text, size_t length) { buffer_.append(text, length); }
    void flush() { buffer_.flush(); }
    PortalWriter& operator+=(const char* text) {
        append(text, strlen(text));
        return *this;
    }
    PortalWriter& operator+=(const String& text) {
        append(text.c_str(), text.length());
        return *this;
    }
    PortalWriter& operator+=(char byte) {
        append(&byte, 1);
        return *this;
    }

private:
    TemplateStream::Buffer<PortalSink> buffer_;
};
struct PortalToken {
    const char* token;
    MsgId message;
    bool javascript;
};
const PortalToken kHeadTokens[] = {
        {"%HTML_TITLE%", MsgId::WEB_HTML_TITLE, false},
        {"%HTML_HEADING%", MsgId::WEB_HTML_HEADING, false},
        {"%HTML_SUBHEAD%", MsgId::WEB_HTML_SUBHEAD, false},
        {"%FONT_FLASH_DELETE%", MsgId::WEB_HTML_FONT_FLASH_DELETE, false},
        {"%FONT_FLASH_TITLE%", MsgId::WEB_HTML_FONT_FLASH_TITLE, false},
        {"%FONT_FLASH_HINT%", MsgId::WEB_HTML_FONT_FLASH_HINT, false},
        {"%FONT_FLASH_INSTALL%", MsgId::WEB_HTML_FONT_FLASH_INSTALL, false},
        {"%FONTS_TITLE%", MsgId::WEB_HTML_FONTS_TITLE, false},
        {"%FONTS_DESC%", MsgId::WEB_HTML_FONTS_DESC, false},
        {"%FONT_SD_TITLE%", MsgId::WEB_HTML_FONT_SD_TITLE, false},
        {"%FONT_SD_UPLOAD%", MsgId::WEB_HTML_FONT_SD_UPLOAD, false},
        {"%TAB_BOOKS%", MsgId::WEB_HTML_TAB_BOOKS, false},
        {"%TAB_SETTINGS%", MsgId::WEB_HTML_TAB_SETTINGS, false},
        {"%SD_FREE_LBL%", MsgId::WEB_HTML_SD_FREE, false},
        {"%UPLOAD_TITLE%", MsgId::WEB_HTML_UPLOAD_TITLE, false},
        {"%UPLOAD_DESC%", MsgId::WEB_HTML_UPLOAD_DESC, false},
        {"%PICK_FILES%", MsgId::WEB_HTML_PICK_FILES, false},
        {"%OVERWRITE_LBL%", MsgId::WEB_HTML_OVERWRITE, false},
        {"%BTN_UPLOAD%", MsgId::WEB_HTML_BTN_UPLOAD, false},
        {"%BOOKS_LIST_TITLE%", MsgId::WEB_HTML_BOOKS_LIST_TITLE, false},
};
const PortalToken kTailTokens[] = {
        {"%BTN_SAVE%", MsgId::WEB_HTML_BTN_SAVE, false},
        {"%BTN_CLOSE%", MsgId::WEB_HTML_BTN_CLOSE_PORTAL, false},
        {"%HTML_FOOTER%", MsgId::WEB_HTML_FOOTER, false},
        {"%JS_NO_BOOKS%", MsgId::WEB_HTML_NO_BOOKS, true},
        {"%JS_DELETE%", MsgId::WEB_HTML_BTN_DELETE, true},
        {"%JS_SAVED%", MsgId::WEB_HTML_STATUS_SAVED, true},
        {"%JS_ERR%", MsgId::WEB_HTML_STATUS_ERROR, true},
        {"%JS_CLOSED%", MsgId::WEB_HTML_PORTAL_CLOSED, true},
        {"%JS_FLASH_BUILTIN%", MsgId::TYPEFACE_BUILTIN, true},
        {"%JS_FLASH_DELETE%", MsgId::WEB_HTML_FONT_FLASH_DELETE, true},
        {"%JS_FLASH_INSTALL%", MsgId::WEB_HTML_FONT_FLASH_INSTALL, true},
        {"%JS_FLASH_FREE%", MsgId::WEB_HTML_FLASH_FREE, true},
        {"%JS_FONT_DELETE_FIRST%", MsgId::WEB_HTML_FONT_DELETE_FIRST, true},
        {"%JS_FONT_BUILTIN_ONLY%", MsgId::WEB_HTML_FONT_BUILTIN_ONLY, true},
        {"%JS_FONT_DELETED%", MsgId::WEB_HTML_FONT_DELETED, true},
        {"%JS_FONT_ALREADY%", MsgId::WEB_HTML_FONT_ALREADY, true},
        {"%JS_FONT_TOO_BIG%", MsgId::WEB_HTML_FONT_TOO_BIG, true},
        {"%JS_FONT_FLASH_DONE%", MsgId::WEB_HTML_FONT_FLASH_DONE, true},
        {"%JS_ZIP_ERROR%", MsgId::WEB_HTML_ZIP_ERROR, true},
        {"%JS_ZIP_BROWSER%", MsgId::WEB_HTML_ZIP_BROWSER, true},
        {"%JS_ZIP_EMPTY%", MsgId::WEB_HTML_ZIP_EMPTY, true},
        {"%JS_ZIP_PREPARING%", MsgId::WEB_HTML_ZIP_PREPARING, true},
        {"%JS_FONTS_EMPTY%", MsgId::WEB_HTML_FONTS_EMPTY, true},
        {"%JS_FONTS_DONE%", MsgId::WEB_HTML_FONTS_DONE, true},
        {"%JS_FONT_EXISTS%", MsgId::WEB_HTML_FONT_EXISTS, true},
        {"%JS_FONT_SD_DELETE%", MsgId::WEB_HTML_FONT_SD_DELETE, true},
        {"%JS_FONT_SD_DELETED%", MsgId::WEB_HTML_FONT_SD_DELETED, true},
        {"%JS_FREE%", MsgId::WEB_HTML_SD_FREE, true},
};
template <size_t N>
void sendPortalTemplate(PortalWriter& out, const char* text, size_t length, const PortalToken (&tokens)[N]) {
    TemplateStream::visit(text, length, [&](const char* part, size_t size, bool token) {
        if (token) {
            if (size == sizeof("%DEVICE%") - 1 && memcmp(part, "%DEVICE%", size) == 0) {
                out += htmlEscape(Platform::kDevice.name);
                return;
            }
            for (const auto& entry : tokens) {
                if (strlen(entry.token) == size && memcmp(part, entry.token, size) == 0) {
                    out += entry.javascript ? jsEscape(tr(entry.message)) : htmlEscape(tr(entry.message));
                    return;
                }
            }
        }
        out.append(part, size);
    });
    out.flush();
}

void beginSection(PortalWriter& out, const char* title, bool open) {
    out += open ? "<details open><summary>" : "<details><summary>";
    out += htmlEscape(title);
    out += "</summary><div class='sec-body'>\n";
}

void endSection(PortalWriter& out) { out += "</div></details>\n"; }

void addInput(PortalWriter& out, const char* type, const char* name, const char* label, const String& value,
              const char* placeholder) {
    out += "<div class='fg'><label class='lbl'>";
    out += htmlEscape(label);
    out += "</label><input type='";
    out += type;
    out += "' name='";
    out += name;
    out += "' value='";
    out += htmlEscape(value);
    out += "' placeholder='";
    out += htmlEscape(placeholder);
    out += "'></div>\n";
}

void addCheckbox(PortalWriter& out, const char* name, const char* label, bool checked) {
    out += "<label class='chk'><input type='checkbox' name='";
    out += name;
    out += "' value='1'";
    if (checked) out += " checked";
    out += ">";
    out += htmlEscape(label);
    out += "</label>\n";
}

void beginSelect(PortalWriter& out, const char* name, const char* label) {
    out += "<div class='fg'><label class='lbl'>";
    out += htmlEscape(label);
    out += "</label><select name='";
    out += name;
    out += "'>\n";
}

void addOption(PortalWriter& out, const String& value, const String& text, bool selected) {
    out += "<option value='";
    out += htmlEscape(value);
    out += "'";
    if (selected) out += " selected";
    out += ">";
    out += htmlEscape(text);
    out += "</option>\n";
}

void endSelect(PortalWriter& out) { out += "</select></div>\n"; }

// Appends a select whose discrete integer presets always include `current` (if from custom config.json).
template <typename FormatFn>
void addIntSelect(PortalWriter& out, const char* name, const char* label, const int* presets, int count, int current,
                  FormatFn formatLabel) {
    std::vector<int> values(presets, presets + count);
    if (std::find(values.begin(), values.end(), current) == values.end()) values.push_back(current);

    beginSelect(out, name, label);
    for (int v : values) {
        addOption(out, String(v), formatLabel(v), v == current);
    }
    endSelect(out);
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool parseIntArg(WebServer& server, const char* name, int& out) {
    if (!server.hasArg(name)) return false;
    const String raw = server.arg(name);
    if (raw.length() == 0) return false;
    char* end = nullptr;
    const long v = strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0') return false;
    out = (int)v;
    return true;
}

String buildPercentText(int pct) { return String(pct) + "%"; }

String buildRefreshText(int pages) {
    if (pages <= 0) return tr(MsgId::APPEAR_REFRESH_NEVER);
    char buf[48];
    snprintf(buf, sizeof(buf), tr(MsgId::APPEAR_REFRESH_FMT), pages);
    return buf;
}

String buildMarginText(int px) {
    char buf[24];
    snprintf(buf, sizeof(buf), tr(MsgId::APPEAR_MARGINS_FMT), px);
    return buf;
}

void streamPortalPage(WebServer& server, const AppConfig& cfg, const std::vector<FontStore::Family>& fonts) {
    MemStats::log("portal page begin");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=utf-8", "");
    PortalWriter out(server);
    sendPortalTemplate(out, HTML_HEAD, sizeof(HTML_HEAD) - 1, kHeadTokens);

    const String passHint = tr(MsgId::WEB_HTML_PASS_KEEP);

    // 1. Server & Wi-Fi (open by default — primary onboarding section)
    beginSection(out, tr(MsgId::WEB_HTML_SEC_NETWORK), true);
    addInput(out, "text", "server_url", tr(MsgId::WEB_HTML_SERVER_URL), cfg.serverHost, "http://192.168.1.100:3000");
    addInput(out, "text", "username", tr(MsgId::WEB_HTML_USERNAME), cfg.bookOrbitUser, "user");
    addInput(out, "password", "password", tr(MsgId::WEB_HTML_PASSWORD), "", passHint.c_str());
    addInput(out, "text", "wifi_ssid", tr(MsgId::WEB_HTML_WIFI_SSID), cfg.wifiSsid, "Wi-Fi SSID");
    addInput(out, "password", "wifi_pass", tr(MsgId::WEB_HTML_WIFI_PASS), "", passHint.c_str());
    endSection(out);

    // 2. Sync schedule
    beginSection(out, tr(MsgId::WEB_HTML_SYNC_HEADING), false);
    addCheckbox(out, "sync_on_close", tr(MsgId::SYNC_ITEM_CLOSE), cfg.syncOnClose);
    addCheckbox(out, "sync_on_sleep", tr(MsgId::SYNC_ITEM_SLEEP), cfg.syncOnSleep);
    addCheckbox(out, "sync_on_wake", tr(MsgId::SYNC_ITEM_WAKE), cfg.syncOnWake);
    addCheckbox(out, "sync_on_boot", tr(MsgId::SYNC_ITEM_BOOT), cfg.syncOnBoot);
    endSection(out);

    // 3. Interface & Device (language, orientation, cover, light, hardware features)
    beginSection(out, tr(MsgId::WEB_HTML_SEC_DEVICE), false);
    beginSelect(out, "lang", tr(MsgId::SETTINGS_ITEM_LANGUAGE));
    for (int i = 0; i < I18n::getLanguageCount(); i++) {
        const AppLanguage l = static_cast<AppLanguage>(i);
        addOption(out, I18n::getLanguageCode(l), I18n::getLanguageName(l), cfg.language == l);
    }
    endSelect(out);

    beginSelect(out, "ui_orient", tr(MsgId::SETTINGS_ITEM_ORIENTATION));
    const MsgId uiOrientLabels[4] = {MsgId::APPEAR_ORIENT_PORTRAIT, MsgId::APPEAR_ORIENT_LANDSCAPE,
                                     MsgId::APPEAR_ORIENT_PORTRAIT_FLIPPED, MsgId::APPEAR_ORIENT_LANDSCAPE_FLIPPED};
    for (int i = 0; i < 4; i++) {
        addOption(out, String(i), tr(uiOrientLabels[i]), cfg.uiOrientation == i);
    }
    endSelect(out);

    if (BoardImu.present()) {
        addCheckbox(out, "auto_rotate", tr(MsgId::APPEAR_AUTO_ROTATE), cfg.autoRotate);
    }

    beginSelect(out, "sleep_cover", tr(MsgId::SETTINGS_ITEM_SLEEP_COVER));
    const String coverVal = !cfg.coverOnSleep ? "off" : (cfg.sleepCoverFitMode == 1 ? "fill" : "fit");
    addOption(out, "fit", tr(MsgId::SLEEP_COVER_MODE_FIT), coverVal == "fit");
    addOption(out, "fill", tr(MsgId::SLEEP_COVER_MODE_FILL), coverVal == "fill");
    addOption(out, "off", tr(MsgId::SLEEP_COVER_MODE_OFF), coverVal == "off");
    endSelect(out);

    if (Frontlight.present()) {
        static constexpr int kPctSteps[] = {0,  5,  10, 15, 20, 25, 30, 35, 40, 45, 50,
                                            55, 60, 65, 70, 75, 80, 85, 90, 95, 100};
        constexpr int kPctCount = sizeof(kPctSteps) / sizeof(kPctSteps[0]);
        addIntSelect(out, "fl_bright", tr(MsgId::FRONTLIGHT_BRIGHTNESS), kPctSteps, kPctCount,
                     clampInt(cfg.frontlight, 0, 100), buildPercentText);
        if (Frontlight.hasWarmth()) {
            addIntSelect(out, "fl_warm", tr(MsgId::FRONTLIGHT_WARMTH), kPctSteps, kPctCount,
                         clampInt(cfg.frontlightWarm, 0, 100), buildPercentText);
        }
    }

    if (hasTouchWakeSetting()) {
        addCheckbox(out, "touch_wake", tr(MsgId::SETTINGS_ITEM_TOUCH_WAKE), cfg.touchWakeEnabled);
    }
    if (hasUsbMscSetting()) {
        addCheckbox(out, "usb_msc", tr(MsgId::USB_MSC_ITEM_TOGGLE), cfg.usbMscEnabled);
    }
    addCheckbox(out, "hide_completed", tr(MsgId::CATALOG_HIDE_COMPLETED), cfg.hideCompleted);
    endSection(out);

    // 4. Reading settings
    beginSection(out, tr(MsgId::SETTINGS_ITEM_READING), false);
    static constexpr int kRefresh[] = {1, 2, 3, 5, 8, 10, 15, 20, 0};
    addIntSelect(out, "full_refresh", tr(MsgId::APPEAR_FULL_REFRESH), kRefresh, sizeof(kRefresh) / sizeof(kRefresh[0]),
                 clampInt(cfg.fullRefreshEvery, 0, 50), buildRefreshText);

    beginSelect(out, "sharp_images", tr(MsgId::APPEAR_IMAGES));
    addOption(out, "0", tr(MsgId::APPEAR_IMAGES_DITHERED), !cfg.sharpImages);
    addOption(out, "1", tr(MsgId::APPEAR_IMAGES_SHARP), cfg.sharpImages);
    endSelect(out);

    if (!Platform::kDevice.upDownTurnPages) {
        beginSelect(out, "up_down_pages", tr(MsgId::APPEAR_UPDOWN_KEYS));
        addOption(out, "0", tr(MsgId::APPEAR_UPDOWN_CHAPTERS), !cfg.upDownTurnPages);
        addOption(out, "1", tr(MsgId::APPEAR_UPDOWN_PAGES), cfg.upDownTurnPages);
        endSelect(out);
    }
    endSection(out);

    // 5. Status bar
    beginSection(out, tr(MsgId::SB_TITLE), false);
    beginSelect(out, "sb_top", tr(MsgId::SB_TOP));
    addOption(out, "0", tr(MsgId::SB_TOP_BOOK), cfg.sbTop == 0);
    addOption(out, "1", tr(MsgId::SB_TOP_CHAPTER), cfg.sbTop == 1);
    addOption(out, "2", tr(MsgId::SB_NONE), cfg.sbTop == 2);
    endSelect(out);

    beginSelect(out, "sb_pages", tr(MsgId::SB_PAGES));
    addOption(out, "0", tr(MsgId::SB_PAGES_BOOK), cfg.sbPages == 0);
    addOption(out, "1", tr(MsgId::SB_PAGES_CHAPTER), cfg.sbPages == 1);
    addOption(out, "2", tr(MsgId::SB_NONE), cfg.sbPages == 2);
    endSelect(out);

    addCheckbox(out, "sb_percent", tr(MsgId::SB_PERCENT), cfg.sbPercent);
    addCheckbox(out, "sb_battery", tr(MsgId::SB_BATTERY), cfg.sbBattery);

    beginSelect(out, "sb_bottom", tr(MsgId::SB_BOTTOM));
    addOption(out, "0", tr(MsgId::SB_BOTTOM_FULL), cfg.sbBottom == 0);
    addOption(out, "1", tr(MsgId::SB_BOTTOM_BAR), cfg.sbBottom == 1);
    addOption(out, "2", tr(MsgId::SB_NONE), cfg.sbBottom == 2);
    endSelect(out);

    addCheckbox(out, "sb_clock", tr(MsgId::SB_CLOCK), cfg.sbClock);

    std::vector<int> tzOffsets;
    tzOffsets.reserve(54);
    for (int m = -720; m <= 840; m += 30) tzOffsets.push_back(m);
    const int curTz = TimeUtils::clampUtcOffset(cfg.utcOffsetMin);
    if (std::find(tzOffsets.begin(), tzOffsets.end(), curTz) == tzOffsets.end()) {
        tzOffsets.push_back(curTz);
        std::sort(tzOffsets.begin(), tzOffsets.end());
    }
    beginSelect(out, "utc_offset", tr(MsgId::SB_TIMEZONE));
    for (int m : tzOffsets) {
        char tzBuf[24];
        TimeUtils::formatUtcOffset(m, tzBuf, sizeof(tzBuf));
        addOption(out, String(m), tzBuf, m == curTz);
    }
    endSelect(out);
    endSection(out);

    // 6. Book Appearance / Typography
    beginSection(out, tr(MsgId::WEB_HTML_SEC_APPEAR), false);
    beginSelect(out, "font_family", tr(MsgId::APPEAR_TYPEFACE));
    const String builtinLabel = String(BookReader::builtinFontName()) + " (" + tr(MsgId::TYPEFACE_BUILTIN) + ")";
    addOption(out, "", builtinLabel, cfg.readerFont.length() == 0);
    const auto installed = FontStore::installedFamilies();
    for (const auto& f : fonts) {
        const bool sel = (cfg.readerFont == f.name);
        addOption(out, f.name, f.name, sel);
    }
    for (const auto& f : installed) {
        if (std::none_of(fonts.begin(), fonts.end(),
                         [&](const FontStore::Family& card) { return card.name == f.name; }))
            addOption(out, f.name, f.name, f.name == cfg.readerFont);
    }
    endSelect(out);

    beginSelect(out, "font_size", tr(MsgId::APPEAR_SIZE));
    for (int i = 0; i < FontEngine::kSizeCount; i++) {
        addOption(out, String(i), BookReader::getFontSizeName(i), cfg.readerFontSize == i);
    }
    endSelect(out);

    static constexpr int kLineSpacing[] = {80, 90, 100, 110, 120, 130, 140, 150, 170, 200};
    addIntSelect(out, "line_spacing", tr(MsgId::APPEAR_LINE_SPACING), kLineSpacing,
                 sizeof(kLineSpacing) / sizeof(kLineSpacing[0]), clampInt(cfg.lineSpacing, 80, 200), buildPercentText);

    static constexpr int kParaSpacing[] = {0, 25, 50, 75, 100, 150, 200};
    addIntSelect(out, "para_spacing", tr(MsgId::APPEAR_PARAGRAPH_SPACING), kParaSpacing,
                 sizeof(kParaSpacing) / sizeof(kParaSpacing[0]), clampInt(cfg.paragraphSpacing, 0, 200),
                 buildPercentText);

    static constexpr int kMargins[] = {6, 10, 14, 20, 26, 32, 40, 50};
    addIntSelect(out, "margin_px", tr(MsgId::APPEAR_MARGINS), kMargins, sizeof(kMargins) / sizeof(kMargins[0]),
                 clampInt(cfg.marginPx, 4, 60), buildMarginText);

    beginSelect(out, "justify", tr(MsgId::APPEAR_ALIGNMENT));
    addOption(out, "1", tr(MsgId::APPEAR_ALIGN_JUSTIFY), cfg.justify);
    addOption(out, "0", tr(MsgId::APPEAR_ALIGN_LEFT), !cfg.justify);
    endSelect(out);

    addCheckbox(out, "hyphenation", tr(MsgId::APPEAR_HYPHENATION), cfg.hyphenation);
    addCheckbox(out, "keep_lines", tr(MsgId::APPEAR_KEEP_LINES), cfg.keepLines);
    addCheckbox(out, "book_styles", tr(MsgId::APPEAR_BOOK_STYLES), cfg.bookStyles);
    addCheckbox(out, "focus_reading", tr(MsgId::APPEAR_FOCUS_READING), cfg.focusReading);

    beginSelect(out, "book_orient", tr(MsgId::APPEAR_ORIENTATION));
    addOption(out, "0", tr(MsgId::APPEAR_ORIENT_AS_UI), cfg.orientation == 0);
    for (int i = 1; i <= 3; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), tr(MsgId::APPEAR_ORIENT_TURNED_FMT), i * 90);
        addOption(out, String(i), buf, cfg.orientation == i);
    }
    endSelect(out);
    endSection(out);

    out.flush();
    sendPortalTemplate(out, HTML_TAIL, sizeof(HTML_TAIL) - 1, kTailTokens);
    server.sendContent("");
    MemStats::log("portal page sent");
}

void applySubmittedConfig(WebServer& server, AppConfig& cfg, bool refreshFont = false) {
    // Track credentials so authToken is only invalidated when server/user/pass actually changed.
    const String oldHost = cfg.serverHost;
    const String oldUser = cfg.bookOrbitUser;
    const String oldSsid = cfg.wifiSsid;
    bool passChanged = false;

    if (server.hasArg("server_url")) cfg.serverHost = server.arg("server_url");
    if (server.hasArg("username")) cfg.bookOrbitUser = server.arg("username");
    if (server.hasArg("wifi_ssid")) cfg.wifiSsid = server.arg("wifi_ssid");
    cfg.serverHost.trim();
    if (cfg.serverHost.length() > 0 && !cfg.serverHost.startsWith("http://") &&
        !cfg.serverHost.startsWith("https://")) {
        cfg.serverHost = "http://" + cfg.serverHost;
    }
    cfg.bookOrbitUser.trim();
    cfg.wifiSsid.trim();

    // Empty password keeps saved one unless the Wi-Fi network itself changed.
    if (server.hasArg("password") && server.arg("password").length() > 0) {
        passChanged = (cfg.bookOrbitPass != server.arg("password"));
        cfg.bookOrbitPass = server.arg("password");
    }
    if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length() > 0) {
        cfg.wifiPass = server.arg("wifi_pass");
    } else if (cfg.wifiSsid != oldSsid) {
        cfg.wifiPass = "";
    }

    if (cfg.serverHost != oldHost || cfg.bookOrbitUser != oldUser || passChanged) {
        cfg.authToken = "";
    }

    cfg.syncOnClose = server.hasArg("sync_on_close");
    cfg.syncOnSleep = server.hasArg("sync_on_sleep");
    cfg.syncOnWake = server.hasArg("sync_on_wake");
    cfg.syncOnBoot = server.hasArg("sync_on_boot");

    if (server.hasArg("lang")) {
        cfg.language = I18n::languageFromCode(server.arg("lang").c_str());
        I18n::setLanguage(cfg.language);
    }

    int v = 0;
    if (parseIntArg(server, "ui_orient", v)) cfg.uiOrientation = clampInt(v, 0, 3);
    if (BoardImu.present()) cfg.autoRotate = server.hasArg("auto_rotate");

    if (server.hasArg("sleep_cover")) {
        const String mode = server.arg("sleep_cover");
        if (mode == "off") {
            cfg.coverOnSleep = false;
        } else {
            cfg.coverOnSleep = true;
            cfg.sleepCoverFitMode = (mode == "fill") ? 1 : 0;
        }
    }

    if (Frontlight.present()) {
        if (parseIntArg(server, "fl_bright", v)) cfg.frontlight = clampInt(v, 0, 100);
        if (Frontlight.hasWarmth() && parseIntArg(server, "fl_warm", v)) {
            cfg.frontlightWarm = clampInt(v, 0, 100);
        }
    }

    if (hasTouchWakeSetting()) cfg.touchWakeEnabled = server.hasArg("touch_wake");
    if (hasUsbMscSetting()) cfg.usbMscEnabled = server.hasArg("usb_msc");
    cfg.hideCompleted = server.hasArg("hide_completed");

    if (parseIntArg(server, "full_refresh", v)) cfg.fullRefreshEvery = clampInt(v, 0, 50);
    if (parseIntArg(server, "sharp_images", v)) cfg.sharpImages = (v != 0);
    if (!Platform::kDevice.upDownTurnPages && parseIntArg(server, "up_down_pages", v)) {
        cfg.upDownTurnPages = (v != 0);
    }

    if (parseIntArg(server, "sb_top", v)) cfg.sbTop = clampInt(v, 0, 2);
    if (parseIntArg(server, "sb_pages", v)) cfg.sbPages = clampInt(v, 0, 2);
    cfg.sbPercent = server.hasArg("sb_percent");
    cfg.sbBattery = server.hasArg("sb_battery");
    if (parseIntArg(server, "sb_bottom", v)) cfg.sbBottom = clampInt(v, 0, 2);
    cfg.sbClock = server.hasArg("sb_clock");
    if (parseIntArg(server, "utc_offset", v)) cfg.utcOffsetMin = TimeUtils::clampUtcOffset(v);

    if (server.hasArg("font_family")) {
        const String want = server.arg("font_family");
        if (want.length() == 0 || (!refreshFont && FontStore::contains(want))) {
            cfg.readerFont = want;
        } else {
            const auto fonts = FontStore::scanCard();
            const auto it = std::find_if(fonts.begin(), fonts.end(),
                                         [&](const FontStore::Family& f) { return f.name == want; });
            if (it != fonts.end()) {
                Reader.setFontFamily("");
                cfg.readerFont = FontStore::install(*it, nullptr, nullptr) ? want : String();
            }
        }
    }

    if (parseIntArg(server, "font_size", v)) cfg.readerFontSize = FontEngine::clampSize(v);
    if (parseIntArg(server, "line_spacing", v)) cfg.lineSpacing = clampInt(v, 80, 200);
    if (parseIntArg(server, "para_spacing", v)) cfg.paragraphSpacing = clampInt(v, 0, 200);
    if (parseIntArg(server, "margin_px", v)) cfg.marginPx = clampInt(v, 4, 60);
    if (parseIntArg(server, "justify", v)) cfg.justify = (v != 0);
    cfg.hyphenation = server.hasArg("hyphenation");
    cfg.keepLines = server.hasArg("keep_lines");
    cfg.bookStyles = server.hasArg("book_styles");
    cfg.focusReading = server.hasArg("focus_reading");
    if (parseIntArg(server, "book_orient", v)) cfg.orientation = clampInt(v, 0, 3);
}

struct PortalUploadState {
    // One request carries every file of the batch (one connection: a sync
    // WebServer that waited on a browser's idle spare connection between two
    // requests let the next upload crawl). State of the file being received:
    FsFile targetFile;
    String targetPath;
    String fileName;
    String partPath;          // written here, renamed to targetPath when complete
    bool rejected = false;
    bool existsSkip = false;
    String errorMsg;
    uint64_t written = 0;
    uint32_t startedAt = 0;
    uint32_t writeUs = 0;     // time spent in card writes (the log tells network from card)
    // The request: results of its files so far, the sizes the page declared.
    String results;           // JSON objects, comma separated
    int part = 0;
    int okCount = 0;
    std::vector<uint64_t> sizes;
    // The body arrives in ~1.4 KB pieces (HTTP_UPLOAD_BUFLEN): gathered into whole
    // 512 B sectors before they go to the card, one SD transaction per 16 KB.
    // Taken once for the portal, so a later file never finds the heap cut up.
    static constexpr size_t kBufCap = 16 * 1024;
    uint8_t* buf = nullptr;
    size_t bufCap = 0;
    size_t bufLen = 0;

    bool takeBuffer() {
        if (buf) return true;
        for (size_t cap = kBufCap; cap >= 2048 && !buf; cap /= 2) {
            buf = (uint8_t*)malloc(cap);
            if (buf) bufCap = cap;
        }
        return buf != nullptr;
    }
    bool flushBuffer() {
        if (!bufLen) return true;
        const uint32_t t0 = micros();
        const size_t w = targetFile.write(buf, bufLen);
        writeUs += micros() - t0;
        const bool ok = w == bufLen;
        bufLen = 0;
        return ok;
    }
    // Appends; false when the card refused a write.
    bool append(const uint8_t* data, size_t n) {
        if (!buf) {
            const uint32_t t0 = micros();
            const bool ok = targetFile.write(data, n) == n;
            writeUs += micros() - t0;
            return ok;
        }
        while (n > 0) {
            const size_t room = bufCap - bufLen, take = n < room ? n : room;
            memcpy(buf + bufLen, data, take);
            bufLen += take;
            data += take;
            n -= take;
            if (bufLen == bufCap && !flushBuffer()) return false;
        }
        return true;
    }
    void dropPart() {
        if (targetFile) targetFile.close();
        if (partPath.length() > 0) Storage.remove(partPath.c_str());
    }
    void reject(const char* why, bool exists = false) {
        rejected = true;
        existsSkip = exists;
        errorMsg = why;
    }
    // The outcome of the file just received, for the response.
    void record(const String& name) {
        if (results.length()) results += ',';
        results += "{\"name\":\"";
        results += jsonEscape(name);
        if (rejected) {
            results += "\",\"ok\":false,\"exists\":";
            results += existsSkip ? "true" : "false";
            results += ",\"error\":\"";
            results += jsonEscape(errorMsg.length() ? errorMsg : String("Upload failed"));
            results += "\"}";
        } else {
            okCount++;
            results += "\",\"ok\":true,\"size\":";
            results += String((unsigned long long)written);
            results += "}";
        }
    }

    PortalUploadState() = default;
    PortalUploadState(const PortalUploadState&) = delete;   // owns buf (freed in the destructor)
    PortalUploadState& operator=(const PortalUploadState&) = delete;

    ~PortalUploadState() {
        if (targetFile && partPath.length() > 0) dropPart();   // the portal closed mid-upload
        free(buf);
    }

    void resetFile() {
        if (targetFile) targetFile.close();
        bufLen = 0;
        partPath = "";
        targetPath = "";
        fileName = "";
        rejected = false;
        existsSkip = false;
        errorMsg = "";
        written = 0;
        startedAt = 0;
        writeUs = 0;
    }
    void resetRequest() {
        resetFile();
        results = "";
        part = 0;
        okCount = 0;
        sizes.clear();
    }
};

static uint64_t s_portalTotalBytes = 0;
static uint64_t s_portalFreeBytes = 0;
static bool s_portalFreeKnown = false;   // counted on the first book list: a FAT scan takes seconds on a big card

void sendBooksListJson(WebServer& server) {
    Platform::feedWatchdog();
    if (!s_portalFreeKnown) {
        s_portalFreeBytes = Storage.freeBytes();
        s_portalFreeKnown = true;
        Platform::feedWatchdog();
    }
    String json;
    json.reserve(1024);

    char hdr[96];
    snprintf(hdr, sizeof(hdr), "{\"total\":%llu,\"free\":%llu,\"books\":[", (unsigned long long)s_portalTotalBytes,
             (unsigned long long)s_portalFreeBytes);
    json += hdr;

    if (Storage.isAvailable()) {
        const auto entries = Storage.listDirectory(LibraryManager::kOfflineDir);
        bool first = true;
        for (const auto& e : entries) {
            Platform::feedWatchdog();
            if (e.isDirectory || e.name.startsWith(".")) continue;
            if (BookFormats::fromPath(e.name.c_str()) == BookFormats::Unknown) continue;
            if (!first) json += ',';
            first = false;
            json += "{\"name\":\"";
            json += jsonEscape(e.name);
            json += "\",\"size\":";
            json += String((unsigned long long)e.size);
            json += "}";
        }
    }
    json += "]}";
    server.send(200, "application/json; charset=utf-8", json);
}

}  // namespace

bool WebConfigManager::runPortal(AppConfig& cfg, WebPortalMode mode) {
    Serial.printf("[WebConfig] Starting Web Configuration Portal (mode=%d)...\n", (int)mode);

    s_portalTotalBytes = Storage.totalBytes();
    s_portalFreeBytes = 0;
    s_portalFreeKnown = false;

    const bool apMode = (mode == WebPortalMode::AccessPoint);
    const char* screenSsid = "BookOrbit-Config";
    char apPass[9] = {};
    String portalUrl;
    IPAddress hostIp;
    DNSServer dnsServer;
    bool dnsStarted = false;

    WifiSession::prepare();
    if (apMode) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        for (int i = 0; i < 8; i++) apPass[i] = (char)('0' + esp_random() % 10);
        apPass[8] = '\0';
        const IPAddress localIp(192, 168, 4, 1);
        const IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(localIp, localIp, subnet);
        if (!WiFi.softAP(screenSsid, apPass)) {
            Serial.println("[WebConfig] Failed to start SoftAP!");
            return false;
        }
        hostIp = WiFi.softAPIP();
        if (hostIp == IPAddress((uint32_t)0)) hostIp = localIp;
        portalUrl = String("http://") + hostIp.toString() + "/";
        Serial.printf("[WebConfig] SoftAP '%s' started at IP: %s\n", screenSsid, hostIp.toString().c_str());
        WiFi.setSleep(false);   // modem sleep halves the upload rate

#if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
        if (WiFi.AP.enableDhcpCaptivePortal()) {
            Serial.println("[WebConfig] DHCP Captive Portal option (RFC 8910) enabled.");
        }
#endif
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsStarted = dnsServer.start(53, "*", hostIp);
        Serial.printf("[WebConfig] DNS server %s on %s:53\n", dnsStarted ? "started" : "FAILED",
                      hostIp.toString().c_str());
    } else {
        if (cfg.wifiSsid.length() == 0) return false;
        screenSsid = cfg.wifiSsid.c_str();
        Ui::status(tr(MsgId::WEB_STA_TITLE), tr(MsgId::WEB_WIFI_CONNECTING), screenSsid, nullptr, nullptr,
                   tr(MsgId::COMMON_CANCEL));
        if (!BookOrbit.connectWiFi(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str(), 12000)) {
            Ui::message(tr(MsgId::WEB_STA_TITLE), tr(MsgId::WEB_WIFI_FAILED_L1), tr(MsgId::WEB_WIFI_FAILED_L2), "", "",
                        2500);
            return false;
        }
        WiFi.setSleep(false);   // modem sleep halves the upload rate
        hostIp = WiFi.localIP();
        portalUrl = String("http://") + hostIp.toString() + "/";
        Serial.printf("[WebConfig] Home Wi-Fi '%s' IP: %s\n", screenSsid, hostIp.toString().c_str());
    }

    Storage.ensureDirectories();
    auto fonts = FontStore::scanCard();
    WebServer server(80);
    PortalUploadState up;
    bool saved = false;
    bool fontsChanged = false;
    bool closeRequested = false;
    bool booksChanged = false;   // the Offline list is scanned again when the portal closes

    auto sendNoCache = [&]() {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate", true);
        server.sendHeader("Pragma", "no-cache", true);
    };

    auto handleRedirect = [&]() {
        PowerService::noteActivity();
        sendNoCache();
        if (apMode) {
            server.sendHeader("Location", portalUrl, true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "Not found");
        }
    };

    if (apMode) {
        server.on("/hotspot-detect.html", handleRedirect);
        server.on("/library/test/success.html", handleRedirect);
        server.on("/generate_204", handleRedirect);
        server.on("/gen_204", handleRedirect);
        server.on("/connecttest.txt", handleRedirect);
        server.on("/ncsi.txt", handleRedirect);
        server.on("/canonical.html", handleRedirect);
        server.on("/success.txt", handleRedirect);
        server.on("/redirect", handleRedirect);
        server.on("/fwlink", handleRedirect);
    }

    server.on("/", HTTP_GET, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        streamPortalPage(server, cfg, fonts);
    });

    server.on("/api/books", HTTP_GET, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        sendBooksListJson(server);
    });

    server.on("/api/fonts", HTTP_GET, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        fonts = Storage.isAvailable() ? FontStore::scanCard() : std::vector<FontStore::Family>();
        String json =
                "{\"builtin\":\"" + jsonEscape(BookReader::builtinFontName()) +
                "\",\"capacity\":" + String(FontStore::capacity()) + ",\"free\":" + String(FontStore::freeBytes()) +
                ",\"largestFree\":" + String(FontStore::largestFreeBytes()) +
                ",\"used\":" + String(FontStore::capacity() - FontStore::freeBytes()) + ",\"installedFamilies\":[";
        const auto installed = FontStore::installedFamilies();
        for (size_t i = 0; i < installed.size(); i++) {
            if (i) json += ',';
            json += "{\"name\":\"" + jsonEscape(installed[i].name) + "\",\"bytes\":" + String(installed[i].bytes) + "}";
        }
        json += "],\"families\":[";
        for (size_t i = 0; i < fonts.size(); i++) {
            if (i) json += ',';
            json += "{\"installed\":" + String(FontStore::contains(fonts[i].name) ? "true" : "false") + ",\"name\":\"" +
                    jsonEscape(fonts[i].name) + "\",\"bytes\":" + String(fonts[i].bytes) +
                    ",\"fits\":" + (FontStore::canInstall(fonts[i]) ? "true" : "false") + "}";
        }
        json += "]}";
        server.send(200, "application/json; charset=utf-8", json);
    });

    server.on("/api/fonts/card/delete", HTTP_POST, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        const String name = server.arg("name");
        if (FontStore::sameFamilyName(name, BookReader::builtinFontName())) {
            server.send(409, "application/json", "{\"ok\":false,\"error\":\"Built-in font cannot be modified\"}");
            return;
        }
        if (!Storage.isAvailable()) {
            server.send(503, "application/json", "{\"ok\":false,\"error\":\"SD card unavailable\"}");
            return;
        }
        bool removed = false;
        // Rescan to include duplicate faces in /fonts and its family subdirectories.
        for (;;) {
            const auto available = FontStore::scanCard();
            const auto found = std::find_if(available.begin(), available.end(), [&](const FontStore::Family& f) {
                return FontStore::sameFamilyName(f.name, name);
            });
            if (found == available.end()) break;
            Reader.setFontFamily("");
            saved = true;
            for (const String& path : found->files) {
                if (!path.length()) continue;
                if (!Storage.remove(path.c_str())) {
                    server.send(500, "application/json", "{\"ok\":false,\"error\":\"Cannot remove SD font\"}");
                    return;
                }
                removed = true;
                fontsChanged = true;
                PowerService::noteActivity();
                Platform::feedWatchdog();
            }
        }
        if (!removed) {
            server.send(404, "application/json", "{\"ok\":false,\"error\":\"Font not found on MicroSD\"}");
            return;
        }
        const bool inFlash = FontStore::contains(name);
        if (!inFlash && FontStore::sameFamilyName(cfg.readerFont, name)) {
            cfg.readerFont = "";
            cfg.save();
        }
        s_portalFreeKnown = false; // Recalculate card space after deleting files.
        server.send(200, "application/json", String("{\"ok\":true,\"inFlash\":") + (inFlash ? "true}" : "false}"));
    });

    server.on("/api/fonts/flash/delete", HTTP_POST, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        const String name = server.arg("name");
        if (FontStore::sameFamilyName(name, BookReader::builtinFontName())) {
            server.send(409, "application/json", "{\"ok\":false,\"error\":\"Built-in font cannot be modified\"}");
            return;
        }
        if (!FontStore::contains(name)) {
            server.send(404, "application/json", "{\"ok\":false}");
            return;
        }
        Reader.setFontFamily("");
        if (!FontStore::removeInstalled(name)) {
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"Cannot remove Flash font\"}");
            return;
        }
        saved = true; // Reapply the active family after releasing its mapping, even when another family was removed.
        if (FontStore::sameFamilyName(cfg.readerFont, name)) {
            cfg.readerFont = "";
            cfg.save();
            saved = true;
        }
        server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/fonts/install", HTTP_POST, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        const String name = server.arg("name");
        if (FontStore::sameFamilyName(name, BookReader::builtinFontName())) {
            server.send(409, "application/json", "{\"ok\":false,\"error\":\"Built-in font cannot be modified\"}");
            return;
        }
        const auto available = FontStore::scanCard();
        const auto found = std::find_if(available.begin(), available.end(),
                                        [&](const FontStore::Family& f) { return f.name == name; });
        if (found == available.end()) {
            server.send(404, "application/json", "{\"ok\":false,\"error\":\"Font not found on MicroSD\"}");
            return;
        }
        if (!FontStore::canInstall(*found)) {
            server.send(413, "application/json; charset=utf-8",
                        "{\"ok\":false,\"error\":\"" + jsonEscape(tr(MsgId::WEB_HTML_FONT_TOO_BIG)) + "\"}");
            return;
        }
        if (FontStore::matchesInstalled(*found)) {
            server.send(200, "application/json", "{\"ok\":true,\"alreadyInstalled\":true}");
            return;
        }
        Reader.setFontFamily("");
        if (!FontStore::install(
                    *found,
                    [](void*, int) {
                        Platform::feedWatchdog();
                        PowerService::noteActivity();
                    },
                    nullptr)) {
            server.send(500, "application/json", "{\"ok\":false,\"error\":\"Font installation failed\"}");
            return;
        }
        cfg.readerFont = name;
        cfg.save();
        saved = true;
        fontsChanged = false;
        server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on(
            "/api/upload", HTTP_POST,
            [&]() {
                PowerService::noteActivity();
                sendNoCache();
                String json = "{\"ok\":";
                json += up.okCount > 0 ? "true" : "false";
                json += ",\"files\":[";
                json += up.results;
                json += "]}";
                server.send(up.part > 0 ? 200 : 400, "application/json; charset=utf-8", json);
                up.resetRequest();
            },
            [&]() {
                HTTPUpload& upload = server.upload();
                const bool fontUpload = server.arg("kind") == "font";
                if (upload.status == UPLOAD_FILE_START) {
                    PowerService::noteActivity();
                    if (up.part == 0) {
                        // Sizes of the files in their order (the page sends them): space check and preallocation.
                        up.sizes.clear();
                        const String s = server.arg("sizes");
                        for (int i = 0; i < (int)s.length();) {
                            int c = s.indexOf(',', i);
                            if (c < 0) c = s.length();
                            up.sizes.push_back((uint64_t)strtoull(s.substring(i, c).c_str(), nullptr, 10));
                            i = c + 1;
                        }
                    }
                    const uint64_t declared = up.part < (int)up.sizes.size() ? up.sizes[up.part] : 0;
                    up.part++;
                    up.resetFile();
                    up.fileName = upload.filename;
                    if (!Storage.isAvailable()) return up.reject("SD card unavailable");
                    String cleanName;
                    if (!sanitizeUploadFilename(upload.filename, cleanName, fontUpload))
                        return up.reject("Unsupported file");
                    up.fileName = cleanName;
                    if (fontUpload) {
                        const String family = FontStore::familyNameForFile(cleanName);
                        if (FontStore::sameFamilyName(family, BookReader::builtinFontName()))
                            return up.reject(tr(MsgId::WEB_HTML_FONT_BUILTIN_ONLY));
                        if (FontStore::contains(family)) return up.reject(tr(MsgId::WEB_HTML_FONT_DELETE_FIRST));
                    }
                    up.targetPath =
                            String(fontUpload ? FontStore::kDir : LibraryManager::kOfflineDir) + "/" + cleanName;
                    up.partPath = up.targetPath + ".part";

                    const String owArg = server.arg("overwrite");
                    const bool overwrite = (owArg == "1" || owArg == "true");
                    if (!overwrite && Storage.exists(up.targetPath.c_str()))
                        return up.reject("File already exists", true);
                    if (declared > 0 && s_portalFreeKnown && declared + 64 * 1024 > s_portalFreeBytes)
                        return up.reject("Not enough space on the SD card");

                    Storage.mkdir(fontUpload ? FontStore::kDir : LibraryManager::kOfflineDir);
                    Storage.remove(up.partPath.c_str());   // a .part of an earlier, broken upload
                    up.targetFile = Storage.open(up.partPath.c_str(), O_WRITE | O_CREAT | O_TRUNC);
                    if (!up.targetFile) {
                        Serial.printf("[WebConfig] Cannot create file: %s\n", up.partPath.c_str());
                        return up.reject("Cannot create file");
                    }
                    up.startedAt = millis();
                    // One run of clusters for the whole book: no FAT walk per cluster while writing.
                    const uint32_t t0 = micros();
                    const bool pre = declared > 0 && up.targetFile.preAllocate(declared);
                    up.writeUs += micros() - t0;
                    up.takeBuffer();
                    const MemStats::Snapshot m = MemStats::now();
                    Serial.printf(
                            "[WebConfig] Receiving %s (%llu B, preallocated: %d, buffer %u KB, heap %u KB free, %u KB block)\n",
                            up.fileName.c_str(), (unsigned long long)declared, (int)pre, (unsigned)(up.bufCap / 1024),
                            (unsigned)(m.freeBytes / 1024), (unsigned)(m.largestBlock / 1024));
                } else if (upload.status == UPLOAD_FILE_WRITE) {
                    if (up.rejected || !up.targetFile || upload.currentSize == 0) return;
                    if (!up.append(upload.buf, upload.currentSize)) {
                        up.dropPart();
                        return up.reject("SD write failed");
                    }
                    up.written += upload.currentSize;
                    // Every ~64 KB: cheap enough, and the watchdog / sleep timer never see a long upload as idle.
                    if ((up.written & 0xFFFF) < upload.currentSize) {
                        PowerService::noteActivity();
                        Platform::feedWatchdog();
                    }
                } else if (upload.status == UPLOAD_FILE_END) {
                    if (!up.rejected) {
                        bool ok = up.targetFile && up.flushBuffer();
                        if (ok) {
                            ok = up.targetFile.truncate(
                                    up.written);   // gives back what preAllocate took beyond the file
                            up.targetFile.flush();
                        }
                        if (up.targetFile) up.targetFile.close();
                        const uint64_t declared = up.part <= (int)up.sizes.size() ? up.sizes[up.part - 1] : 0;
                        if (fontUpload && (up.written == 0 || (declared && declared != up.written))) ok = false;
                        if (ok && fontUpload) {
                            FsFile check = Storage.open(up.partPath.c_str(), O_RDONLY);
                            uint8_t signature[12] = {};
                            const bool headerRead =
                                    check && check.read(signature, sizeof(signature)) == sizeof(signature);
                            if (check) check.close();
                            ok = headerRead &&
                                 ((signature[0] == 0 && signature[1] == 1 && signature[2] == 0 && signature[3] == 0) ||
                                  memcmp(signature, "OTTO", 4) == 0 || memcmp(signature, "true", 4) == 0);
                            if (!ok) up.reject("Invalid TTF/OTF font");
                        }
                        if (ok && !fontUpload && Storage.exists(up.targetPath.c_str())) {
                            // Replaced by a new copy: its page cache, cover and position go with the old one.
                            BookItem old;
                            old.filePath = up.targetPath;
                            LibraryManager::deleteBookArtifacts(old, true);
                        }
                        ok = ok && Storage.replaceFile(up.partPath.c_str(), up.targetPath.c_str());
                        if (ok) {
                            if (fontUpload)
                                fontsChanged = true;
                            else
                                booksChanged = true;
                            s_portalFreeKnown = false;
                            const uint32_t ms = millis() - up.startedAt;
                            Serial.printf(
                                    "[WebConfig] Uploaded %s: %s (%llu B in %lu ms, %lu KB/s; card writes %lu ms)\n",
                                    fontUpload ? "font" : "offline book", up.targetPath.c_str(),
                                    (unsigned long long)up.written, (unsigned long)ms,
                                    (unsigned long)(ms ? up.written / ms : 0), (unsigned long)(up.writeUs / 1000));
                            if (s_portalFreeBytes >= up.written)
                                s_portalFreeBytes -= up.written;
                            else
                                s_portalFreeBytes = 0;
                        } else {
                            if (!up.rejected) up.reject("SD write failed");
                        }
                    }
                    if (up.rejected) up.dropPart();
                    up.record(up.fileName);
                    PowerService::noteActivity();
                    Platform::feedWatchdog();
                } else if (upload.status == UPLOAD_FILE_ABORTED) {
                    up.dropPart();
                    up.reject("Upload aborted");
                    up.record(up.fileName);
                }
            });

    server.on("/api/books/delete", HTTP_POST, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        String cleanName;
        if (!sanitizeUploadFilename(server.arg("name"), cleanName)) {
            server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Invalid filename\"}");
            return;
        }
        const String path = String(LibraryManager::kOfflineDir) + "/" + cleanName;
        if (!Storage.exists(path.c_str())) {
            server.send(404, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"File not found\"}");
            return;
        }
        uint64_t fSz = 0;
        FsFile f = Storage.open(path.c_str(), O_RDONLY);
        if (f) {
            fSz = f.size();
            f.close();
        }
        BookItem item;
        item.filePath = path;
        LibraryManager::deleteBookArtifacts(item, true);
        LibraryManager::getInstance().updateLastBookAfterDeletion(path);
        s_portalFreeBytes += fSz;
        if (s_portalFreeBytes > s_portalTotalBytes) s_portalFreeBytes = s_portalTotalBytes;
        booksChanged = true;
        Serial.printf("[WebConfig] Deleted offline book: %s\n", path.c_str());
        server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
    });

    server.on("/save", HTTP_POST, [&]() {
        PowerService::noteActivity();
        applySubmittedConfig(server, cfg, fontsChanged);
        fontsChanged = false;
        cfg.save();
        saved = true;
        Serial.println("[WebConfig] New settings saved from web portal!");

        if (server.arg("ajax") == "1") {
            sendNoCache();
            server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
            return;
        }

        String savedPage = FPSTR(HTML_SAVED);
        savedPage.replace("%SAVED_TITLE%", htmlEscape(tr(MsgId::WEB_HTML_SAVED_TITLE)));
        savedPage.replace("%SAVED_DESC%", htmlEscape(tr(MsgId::WEB_HTML_SAVED_DESC)));
        savedPage.replace("%SAVED_HINT%", htmlEscape(tr(MsgId::WEB_HTML_SAVED_HINT)));
        savedPage.replace("%TAB_BOOKS%", htmlEscape(tr(MsgId::WEB_HTML_TAB_BOOKS)));
        server.send(200, "text/html; charset=utf-8", savedPage);
    });

    server.on("/api/close", HTTP_POST, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
        closeRequested = true;
    });

    server.onNotFound(handleRedirect);
    server.begin();
    Serial.println("[WebConfig] HTTP server listening on port 80.");

    int page = apMode ? 0 : -1;
    Buttons.clearEvents();
    Ui::portalScreen(screenSsid, apMode ? apPass : "", portalUrl.c_str(), page, true);   // drawn by the UI (A5)
    PowerService::noteActivity();
    Buttons.clearEvents();

    uint32_t lastKeysAt = 0;
    while (!closeRequested) {
        if (dnsStarted) dnsServer.processNextRequest();
        server.handleClient();
        Platform::feedWatchdog();

        // Keys and touch every 20 ms: pollModalEvent waits 10 ms each time, which
        // at every turn held back the server's next request.
        const uint32_t now = millis();
        if (now - lastKeysAt < 20) {
            delay(1);
            continue;
        }
        lastKeysAt = now;
        ButtonEvent event;
        if (PowerService::pollModalEvent(event)) {
            const PortalAction act = Ui::portalEvent(event.id, event.action, event.x, event.y, page);
            if (act == PortalAction::Close) {
                Serial.println("[WebConfig] Closed by user.");
                break;
            }
            if (act == PortalAction::Redraw) {
                Ui::portalScreen(screenSsid, apMode ? apPass : "", portalUrl.c_str(), page, false);
            }
        }
    }

    if (closeRequested) delay(300);

    if (dnsStarted) dnsServer.stop();
    server.stop();
    if (apMode) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(100);
    } else {
        BookOrbit.disconnectWiFi();
    }
    Serial.println("[WebConfig] Web Configuration Portal closed.");
    if (booksChanged) {
        LibraryManager::getInstance().scanOfflineBooks();
        Ui::coverCacheChanged();
    }

    return saved;
}

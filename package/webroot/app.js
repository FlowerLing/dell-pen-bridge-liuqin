'use strict';
const root = '/data/adb/modules/dell_pen_bridge_liuqin';
const preview = new URLSearchParams(location.search).has('preview');
const options = [['none','不执行'],['back','返回'],['home','回到桌面'],['recents','最近任务'],['volume_up','音量增加'],['volume_down','音量减少'],['play_pause','播放 / 暂停'],['next','下一曲'],['previous','上一曲'],['screenshot','截图（取决于系统支持）'],['custom','自定义命令']];
const gestures = [['single','单击','轻按一次'],['double','双击','快速连按两次'],['long','长按','按住约一秒后松开']];
let sequence = 0, loaded = false;
const $ = id => document.getElementById(id);
const quote = value => "'" + String(value).replaceAll("'", "'\\''") + "'";
const encode = value => btoa(String.fromCharCode(...new TextEncoder().encode(value)));
const decode = value => new TextDecoder('utf-8',{fatal:true}).decode(Uint8Array.from(atob(value),x=>x.charCodeAt(0)));
function shell(command) {
  return new Promise((resolve,reject)=>{
    if(!window.ksu || typeof window.ksu.exec!=='function') { reject(new Error('请从 KernelSU 的模块 WebUI 打开此页'));return; }
    const callback='dell_top_'+Date.now()+'_'+sequence++;
    const timer=setTimeout(()=>{delete window[callback];reject(new Error('操作超时，请刷新状态后重试'));},15000);
    window[callback]=(code,out,err)=>{clearTimeout(timer);delete window[callback];Number(code)===0?resolve(out):reject(new Error(err||out||'操作失败'));};
    try {window.ksu.exec(command,'{}',callback);} catch(error){clearTimeout(timer);delete window[callback];reject(error);}
  });
}
function api(...args) {return shell('/data/adb/ksu/bin/busybox sh '+quote(root+'/bin/top-config.sh')+' '+args.map(quote).join(' '));}
for(const [key,title,caption] of gestures){
  const section=document.createElement('section');section.className='gesture';
  const label=document.createElement('label');label.htmlFor=key;label.textContent=title;
  const sub=document.createElement('p');sub.className='caption';sub.textContent=caption;
  const select=document.createElement('select');select.id=key;
  for(const [value,text] of options){const option=document.createElement('option');option.value=value;option.textContent=text;select.append(option);}
  const command=document.createElement('div');command.className='command';command.hidden=true;
  const cl=document.createElement('label');cl.htmlFor=key+'-command';cl.textContent='Shell 命令';
  const textarea=document.createElement('textarea');textarea.id=key+'-command';textarea.maxLength=4096;textarea.spellcheck=false;textarea.placeholder='例如：input keyevent 3';
  const help=document.createElement('p');help.textContent='支持多行；最多 4096 个 UTF-8 字节。';
  command.append(cl,textarea,help);select.addEventListener('change',()=>{command.hidden=select.value!=='custom';});
  section.append(label,sub,select,command);$('gestures').append(section);
}
async function refresh(){
  if(preview){$('connection').textContent='界面预览 · 不连接设备';return;}
  const status=JSON.parse(await api('status'));
  $('connection').textContent=!status.running?'顶部监听未运行':!status.enabled?'顶部按键已关闭':status.connected?'戴尔笔蓝牙已连接':'等待戴尔笔蓝牙连接';
}
async function load(){
  $('save').disabled=true;
  try{
    const rows=preview?['0','none','none','none','','','']:(await api('get')).replaceAll('\r','').split('\n');
    $('enabled').checked=rows[0]==='1';
    gestures.forEach(([key],i)=>{$(key).value=options.some(x=>x[0]===rows[i+1])?rows[i+1]:'none';$(key+'-command').value=decode(rows[i+4]||'');$(key).dispatchEvent(new Event('change'));});
    loaded=true;await refresh();$('message').textContent=preview?'预览模式，保存不会写入设备':'设置已读取';
  }catch(e){$('message').textContent=e.message;}finally{$('save').disabled=!loaded;}
}
$('refresh').addEventListener('click',()=>refresh().catch(e=>{$('message').textContent=e.message;}));
$('settings').addEventListener('submit',async event=>{
  event.preventDefault();if(!loaded)return;
  $('save').disabled=true;
  try{
    const commands=gestures.map(([key])=>$(key+'-command').value);
    if(commands.some(x=>new TextEncoder().encode(x).length>4096||x.includes('\0')))throw new Error('命令超过 4096 字节或含无效字符');
    const args=[$('enabled').checked?'1':'0',...gestures.map(([key])=>$(key).value),...commands.map(encode)];
    if(preview){$('message').textContent='预览：设置有效，未写入设备';return;}
    await api('save',...args);
    await shell('/data/adb/ksu/bin/busybox sh '+quote(root+'/bin/top-control.sh')+' start');
    $('message').textContent='已保存，下次按键生效';setTimeout(()=>refresh().catch(()=>{}),1200);
  }catch(e){$('message').textContent=e.message;}finally{$('save').disabled=false;}
});
load();

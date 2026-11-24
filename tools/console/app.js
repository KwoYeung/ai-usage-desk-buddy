const statusLabels = {starting:'正在启动', connecting:'正在连接', connected:'已连接', retrying:'正在重连', stopping:'正在断开', disconnected:'未连接', error:'连接异常', saved:'设置已保存'};
const messages = {
  'Disconnect before changing settings':'请先断开连接，再修改设置',
  'Invalid transport':'连接方式无效', 'Invalid setting':'设置内容无效',
  'Use a local IPv4 address':'请输入局域网 IPv4 地址', 'Invalid port':'端口应为 1 至 65535 之间的整数',
  'Invalid autoconnect setting':'自动连接设置无效', 'Invalid pairing file':'配对文件格式不正确',
  'Invalid pairing key':'配对密钥无效', 'Settings saved on this computer':'设置已保存在本机',
  'Enter device IP and import pairing file first':'请先搜索设备并完成配对；请在连接页重新搜索并核对六位配对码',
  'Connect first':'请先连接设备', 'Please wait five seconds between tests':'请间隔 5 秒再测试提醒',
  'Disconnect before scanning':'请先断开连接，再搜索设备', 'Unknown action':'不支持此操作',
  'Forbidden':'请求校验失败，请刷新网页后重试', 'Invalid request size':'请求内容大小无效',
  'Invalid request':'请求内容格式不正确',
  'Completion sent; sound follows device volume and quiet hours':'完成事件已发送；提示音遵循设备音量和夜间静音设置'
};
function localize(message) {
  if (messages[message]) return messages[message];
  if (statusLabels[message]) return statusLabels[message];
  if (/FileNotFoundError/.test(message)) return '未检测到 Codex，请先打开并登录 Codex 桌面应用或 CLI，然后重新连接；特殊安装位置可在高级设置中手动指定';
  if (/Usage refresh failed/.test(message)) return '用量刷新失败，稍后自动重试';
  if (/Task refresh failed/.test(message)) return '任务状态刷新失败，稍后自动重试';
  if (/TimeoutError/.test(message)) return '连接超时，请检查设备、网络或是否有其他电脑占用连接';
  if (/check device, network and Codex login/.test(message)) return '连接异常，请检查设备、网络和 Codex 登录状态';
  if (/operation failed/.test(message)) return '操作失败，请检查设备连接及系统权限后重试';
  if (/JSON|Unexpected token|Unexpected end|non-hexadecimal/.test(message)) return '文件或请求格式不正确，请使用有效的配对 JSON 文件';
  if (/Failed to fetch|NetworkError|fetch/.test(message)) return '无法连接本机控制台，请确认后台程序仍在运行';
  if (/Address|Octet|octet|IPv4|Expected 4/.test(message)) return 'IP 地址格式不正确，请输入例如 192.168.1.100 的地址';
  return message;
}
const $ = id => document.getElementById(id);
const token = document.querySelector('meta[name="passport-token"]').content;
let initialized = false, pairing = null, busy = false, exited = false;
let onboarding = false, guideStep = 'environment', editing = false, lastState = null;
let selectedPage = 'status', selectedSubpage = 'codex';
document.querySelectorAll('[data-page]').forEach(button => {
  button.onclick = () => { selectedPage = button.dataset.page; if(lastState) renderNavigation(lastState); };
});
document.querySelectorAll('[data-subpage]').forEach(button => {
  button.onclick = () => { selectedSubpage = button.dataset.subpage; if(lastState) renderNavigation(lastState); };
});
function deepseekError(code) {
  const errors = {invalid_key:'API Key 无效，请检查后重试', rate_limited:'DeepSeek 接口请求过于频繁，请稍后再试', network:'无法连接 DeepSeek，请检查网络或代理', bad_response:'DeepSeek 返回的数据格式异常'};
  return errors[code] || (code ? '余额同步失败（' + code + '）' : '');
}
function deepseekMoney(value) { return (Number(value || 0) / 100).toFixed(2); }
function renderDeepseek(state) {
  const d = state.deepseek || {};
  const configured = Boolean(d.configured), available = Boolean(d.available);
  const amount = deepseekMoney(d.total);
  $('deepseek-total').textContent = available ? amount : '—';
  $('deepseek-currency').textContent = available && d.currency ? d.currency : '';
  $('deepseek-badge').textContent = !configured ? '未配置' : (available ? (d.stale ? '缓存' : '已同步') : '等待同步');
  $('deepseek-badge').style.color = !configured ? 'var(--muted)' : (available && !d.stale ? 'var(--green)' : 'var(--gold)');
  const usage = state.dsh_usage || {};
  const usageAvailable = Boolean(usage.available);
  $('deepseek-7d').textContent = usageAvailable ? tokenLabel(usage.tokens_7d) : '—';
  $('deepseek-30d').textContent = usageAvailable ? tokenLabel(usage.tokens_30d) : '—';
  $('deepseek-daily').textContent = usageAvailable
    ? '今天 ' + tokenLabel(usage.tokens_today) + ' · 请求 ' + (usage.requests_today || 0)
    : '今天 — · 请求 —';
  $('deepseek-usage-hint').textContent = usageAvailable
    ? 'Token 含缓存读，来自本机 DSH 会话日志，无需 API Key。'
    : (usage.error === 'zstd_unavailable' ? '无法读取 DSH 日志：缺少 zstd 支持。'
      : (usage.error === 'no_sessions' ? '未找到本机 DSH 会话日志。' : '正在读取本机 DSH 会话日志…'));
  const updated = d.updated ? new Date(d.updated * 1000).toLocaleString('zh-CN') : '';
  $('deepseek-updated').textContent = available ? (d.stale ? '上次成功更新：' + updated : '更新于 ' + updated) : '尚未从 DeepSeek 读取余额。';
  $('deepseek-error').textContent = deepseekError(d.error);
  $('deepseek-key').placeholder = configured ? '已保存（不显示）；输入新 Key 可替换' : 'sk-...';
}
function tokenLabel(value) {
  if (value === null || value === undefined || value > Number.MAX_SAFE_INTEGER) return '—';
  if(value >= 999950000) return (value / 1e9).toFixed(value >= 1e10 ? 1 : 2) + 'B';
  return value >= 1e6 ? (value/1e6).toFixed(1)+'M' : String(value);
}
let lanDevices=[], pairSession=null, completedPair=null;
async function api(action, data) {
  const response = await fetch('/api/' + action, {method:data === undefined?'GET':'POST',headers:{'X-Passport-Token':token,'Content-Type':'application/json'},body:data === undefined?undefined:JSON.stringify(data)});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || '请求失败');
  return result;
}
function notice(message, error=false) { $('notice').textContent=message; $('notice').classList.toggle('error',error); }
function mode() { return document.querySelector('input[name=mode]:checked').value; }
function toggleMode() { $('lan-fields').hidden=mode()!=='lan'; $('ble-fields').hidden=mode()!=='ble'; $('connection-help').textContent=mode()==='ble'?'打开设备设置中的蓝牙开关，让设备靠近电脑，再点击搜索。':'让设备和电脑连接同一路由器，点击搜索，找到后核对校验码完成配对。'; }
document.querySelectorAll('[name=mode]').forEach(el=>el.addEventListener('change',toggleMode));
function configuration() { const cfg={mode:mode(),host:$('host').value,device:$('device').value,port:Number($('port').value),codex:$('codex').value,codex_auto:$('codex-auto').checked,autoconnect:$('autoconnect').checked}; if(pairing)cfg.pairing=pairing; return cfg; }
async function operation(work) { if(busy)return;busy=true;document.body.setAttribute('aria-busy','true');try{await work();if(!exited)await refresh();}catch(error){notice(localize(error.message),true);}finally{busy=false;document.body.removeAttribute('aria-busy');} }
async function save(){await api('save',configuration());pairing=null;notice('设置已保存。');}
$('settings').addEventListener('submit',event=>{event.preventDefault();operation(async()=>{if(mode()==='ble'&&!$('device').value.trim())throw new Error('请先搜索并选择你的 Passport。');await save();const result=await api('connect',{});notice('正在连接… 首次蓝牙配对可能会弹出系统配对窗口。');});});
$('save').onclick=()=>operation(save);
$('disconnect').onclick=()=>operation(async()=>{await api('disconnect',{});notice('已断开连接，设置已保留，下次可直接连接。');});
$('test').onclick=()=>operation(async()=>{await api('test',{});notice('提示音测试已排队。请在 Passport 上听取测试音。');});
$('deepseek-form').onsubmit=event=>{event.preventDefault();operation(async()=>{const key=$('deepseek-key').value.trim();if(!key)throw new Error('请输入 DeepSeek API Key');const result=await api('deepseek_save',{key});$('deepseek-key').value='';const error=result.deepseek?.error;notice(error?'Key 已保存，但验证失败：'+deepseekError(error):(result.deepseek?.available?'DeepSeek 余额已更新。':'Key 已保存，正在等待余额同步。'),Boolean(error));});};
$('deepseek-refresh').onclick=()=>operation(async()=>{const result=await api('deepseek_refresh',{});const error=result.deepseek?.error;const usageReady=Boolean(result.dsh_usage?.available);notice(error?deepseekError(error):(result.deepseek?.available?'DeepSeek 余额已刷新。':(usageReady?'Token 统计已刷新。':'余额刷新已排队，请稍后查看。')),Boolean(error));});
$('deepseek-clear').onclick=()=>operation(async()=>{await api('deepseek_clear',{});notice('已清除本机保存的 DeepSeek API Key。');});
$('scan').onclick=()=>operation(async()=>{notice('正在搜索蓝牙设备…');$('scan').disabled=true;try{const result=await api('scan',{});$('devices').replaceChildren();for(const device of result.devices){const option=document.createElement('option');option.value=device.address;option.textContent=device.name+' · '+device.address;$('devices').append(option);}$('devices').hidden=!result.devices.length;$('devices-label').hidden=!result.devices.length;if(result.devices.length===1)$('device').value=result.devices[0].address;else if(result.devices.length>1){$('devices').prepend(new Option('请选择你的设备',''));$('devices').value='';$('device').value='';}notice(result.devices.length?'请在下方选择你的 Passport。':'未找到 Passport，请确认设备处于蓝牙模式并靠近电脑。');}finally{$('scan').disabled=false;}});
$('devices').onchange=()=>{$('device').value=$('devices').value;};
async function refresh(){const state=await api('status');lastState=state;if(state.pairing?.status==='success'&&completedPair!==state.pairing.id){completedPair=state.pairing.id;initialized=false;notice('配对已保存，正在连接设备…');}if(!initialized){onboarding=!state.config.setup_complete;for(const id of ['host','device','port','codex'])$(id).value=state.config[id];$('autoconnect').checked=state.config.autoconnect;$('codex-auto').checked=state.config.codex_auto!==false;$('codex-manual').hidden=$('codex-auto').checked;document.querySelector(`input[name=mode][value="${state.config.mode}"]`).checked=true;toggleMode();
if(onboarding){$('autoconnect').checked=true;checkEnvironment();}notice(state.status==='connected'?'设备已连接。':'请选择连接方式。');initialized=true;}
renderNavigation(state);
$('weekly-tokens').textContent=tokenLabel(state.token_stats?.weekly);
$('monthly-tokens').textContent=tokenLabel(state.token_stats?.monthly);
const active=['starting','connecting','connected','retrying','stopping'].includes(state.status),connected=state.status==='connected';$('badge').textContent=(statusLabels[state.status] || '状态未知');$('activity').textContent=connected?(state.running?'工作中':'准备就绪'):(active?'正在连接…':'等待连接');$('detail').textContent=connected?'设备正在接收实时状态更新。':(active?(state.config.mode==='ble'?'请保持设备蓝牙开启；首次连接请在系统窗口输入设备上的六位配对码。':'请保持设备联网，并确认没有被其他电脑占用。'):'连接后显示 Codex 用量与任务状态。');$('tasks').textContent=connected?state.running:'—';$('sync').textContent=state.synced_at?new Date(state.synced_at*1000).toLocaleTimeString('zh-CN', {hour:'2-digit',minute:'2-digit'}):'—';$('test').disabled=!connected;const dshState=state.dsh||{};$('dsh-summary').textContent=!dshState.available?'DSH 状态：未连接':(dshState.working?'DSH 状态：工作中':'DSH 状态：就绪');$('dsh-summary').style.color=dshState.available&&dshState.working?'var(--green)':'';$('connect').hidden=active;$('save').hidden=active;$('disconnect').hidden=!active;for(const input of document.querySelectorAll('#settings input, #settings select, #scan'))input.disabled=active;$('key-state').textContent=pairing?'待保存':state.key_present?'✓ 配对密钥已保存在本机':'尚未保存配对密钥';$('usage').replaceChildren();for(const key of ['primary','secondary']){const w=state.usage[key];if(!w||!w.duration)continue;const remaining=Math.max(0,Math.min(100,100-w.used));const row=document.createElement('p');const label=document.createElement('span');label.textContent=w.duration>=1440?`${w.duration/1440} 天`:`${w.duration/60} 小时`;const value=document.createElement('span');value.textContent=`剩余 ${Math.round(remaining)}%`;row.append(label,value);const bar=document.createElement('progress');bar.max=100;bar.value=remaining;bar.dataset.window=w.duration===10080?'weekly':'standard';$('usage').append(row,bar);}$('logs').replaceChildren();for(const entry of state.logs.slice().reverse()){const li=document.createElement('li'),time=document.createElement('time');time.textContent=new Date(entry.at*1000).toLocaleTimeString('zh-CN');li.append(time,document.createTextNode(localize(entry.message)));$('logs').append(li);}renderDeepseek(state);renderPairing(state);}
async function poll(){if(exited)return;try{await refresh();}catch(error){notice('无法连接本机控制台，请重新运行启动程序。',true);}setTimeout(poll,2000);}poll();

$('exit-console').onclick=()=>operation(async()=>{if(window.pywebview?.api){await window.pywebview.api.quit();return;}await api('exit',{});exited=true;document.querySelectorAll('button,input,select').forEach(el=>el.disabled=true);notice('后台已退出，设备已断开。如果页面未自动关闭，可以手动关闭此页。');window.close();});

$('codex-auto').onchange=()=>{$('codex-manual').hidden=$('codex-auto').checked;};


function renderNavigation(state) {
  const connected=state.status==='connected';
  const active=['starting','connecting','connected','retrying','stopping'].includes(state.status);
  if(onboarding && connected && state.synced_at) guideStep='ready';
  if(onboarding && guideStep==='ready' && !connected) guideStep='device';
  document.body.classList.toggle('onboarding-device',onboarding && guideStep==='device');
  $('welcome').hidden=!onboarding;
  $('home-actions').hidden=onboarding;
  $('environment-panel').hidden=guideStep!=='environment';
  $('ready-panel').hidden=guideStep!=='ready';
  $('workspace').hidden=onboarding && guideStep==='environment';
  $('connection-panel').hidden=onboarding?guideStep!=='device':selectedPage!=='connection';
  $('status-panel').hidden=onboarding || selectedPage!=='status';
  $('codex-view').hidden=selectedSubpage!=='codex';
  $('deepseek-view').hidden=selectedSubpage!=='deepseek';
  document.querySelectorAll('[data-page]').forEach(button => button.setAttribute('aria-current',button.dataset.page===selectedPage?'page':'false'));
  document.querySelectorAll('[data-subpage]').forEach(button => button.setAttribute('aria-selected',button.dataset.subpage===selectedSubpage?'true':'false'));
  $('workspace').classList.add('dashboard');
  $('close-settings').hidden=onboarding;
  $('quick-connect').hidden=active;
  $('edit-settings').setAttribute('aria-expanded',String(editing));
  $('guide-title').textContent={environment:'连接你的 Passport',device:'找到你的 Passport',ready:'连接成功'}[guideStep];
  $('guide-description').textContent=guideStep==='device'?'设备连上 Wi-Fi 后可直接搜索配对，也可以选择蓝牙。':'配置连接方式并完成首次同步。';
  for(const step of ['environment','device','ready']) {
    if(step===guideStep) $('step-'+step).setAttribute('aria-current','step');
    else $('step-'+step).removeAttribute('aria-current');
  }
}
async function checkEnvironment() {
  $('check-environment').disabled=true;
  $('environment-result').textContent='正在检查电脑…';
  try {
    const result=await api('check_environment',{});
    $('environment-result').textContent=result.codex_found?'✓ 已找到 Codex。连接设备后将验证登录与同步。':'尚未找到 Codex。请打开并登录 Codex 后重新检查，也可继续并在高级设置中指定程序位置。';
  } catch(error) { $('environment-result').textContent=localize(error.message); }
  finally { $('check-environment').disabled=false; }
}
$('check-environment').onclick=checkEnvironment;
$('guide-next').onclick=()=>{guideStep='device';renderNavigation(lastState);notice(mode()==='lan'?'设备连上 Wi-Fi 后，点击“搜索局域网设备”。':'打开设备蓝牙，点击“搜索蓝牙设备”。');};
$('finish-setup').onclick=()=>operation(async()=>{await api('finish_setup',{});onboarding=false;editing=false;selectedPage="status";notice('设置完成。');});
$('edit-settings').onclick=()=>{selectedPage='connection';renderNavigation(lastState);};
$('close-settings').onclick=()=>{editing=false;selectedPage="status";renderNavigation(lastState);};
$('quick-connect').onclick=()=>operation(async()=>{await api('connect',{});notice('正在连接已保存的设备…');});
window.addEventListener('pywebviewready',()=>{$('lifecycle-hint').textContent='关闭窗口后在托盘继续运行；右键托盘图标可以退出。';});

async function scanLan(host='') { return operation(async()=>{
  $('scan-lan').disabled=true;
  $('lan-scan-result').textContent=host?'正在查找指定 IP 的 Passport…':'正在寻找同一网络中的 Passport…';
  try {
    const result=await api('scan_lan',host?{host}:{});lanDevices=result.devices;
    $('lan-devices').replaceChildren(new Option('请选择你的设备',''));
    for(let i=0;i<lanDevices.length;i++) $('lan-devices').append(new Option(lanDevices[i].name+(lanDevices[i].available?'':' · 已被连接'),String(i)));
    $('lan-devices-label').hidden=!lanDevices.length;
    $('pair-lan').hidden=!lanDevices.length;
    if(lanDevices.length===1)$('lan-devices').value='0';
    $('lan-scan-result').textContent=lanDevices.length?'找到设备。请选择要配对的那一台。':(host?'该 IP 未响应。请检查设备地址、网络连通性和固件版本。':'未找到设备。跨子网可展开“按 IP 查找”；旧固件可展开手动连接。');
  }finally{$('scan-lan').disabled=false;}
});}
$('scan-lan').onclick=()=>scanLan();
$('find-lan-ip').onclick=()=>{const host=$('discover-host').value.trim();if(!host){notice('请输入设备 IP 地址',true);return;}return scanLan(host);};
$('pair-lan').onclick=()=>operation(async()=>{
  const value=$('lan-devices').value;
  if(value==='')throw Error('请选择你的设备');
  const device=lanDevices[Number(value)];
  if(!device?.available)throw Error('请先在旧电脑上断开这台设备');
  await save();
  const result=await api('pair_start',{id:device.id,host:device.host});
  pairSession=result.id;notice('请核对电脑和设备上的号码，只在一致时确认。30 秒后自动取消。');
});
$('pair-confirm').onclick=()=>operation(async()=>{await api('pair_confirm',{id:pairSession});notice('等待设备确认，请在设备上按 OK。');});
$('pair-cancel').onclick=()=>operation(async()=>{await api('pair_cancel',{id:pairSession});pairSession=null;notice('已取消配对，原有配对信息不变。');});
function renderPairing(state){
  const pair=state.pairing;
  const pending=pair&&['compare','waiting_device'].includes(pair.status);
  if(pending){editing=true;selectedPage="connection";guideStep='device';renderNavigation(state);}
  $('connect').disabled=Boolean(pending);$('save').disabled=Boolean(pending);
  $('pair-panel').hidden=!pending;
  if(pending){pairSession=pair.id;$('pair-code').textContent=pair.code;$('pair-confirm').disabled=pair.status==='waiting_device';$('pair-instruction').textContent=pair.status==='waiting_device'?'电脑已确认，请在设备上按 OK；未操作会自动超时。':'核对两端号码，一致时在设备上按 OK，并点击下方确认。不同则取消。';}
  for(const element of document.querySelectorAll('#settings input,#settings select,#scan,#scan-lan,#find-lan-ip,#pair-lan,#connect,#save')) {
    if(pending)element.disabled=true;
  }
  if(!pending){$('find-lan-ip').disabled=false;$('scan-lan').disabled=false;$('pair-lan').disabled=false;}
  $('paired-state').textContent=state.key_present?'✓ 已保存配对，之后可直接连接。':'';
  if(pair?.status==='error')notice(pair.message,true);
}

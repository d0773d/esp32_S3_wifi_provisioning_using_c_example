function toggleTheme(){
const html=document.documentElement;
const body=document.body;
const icon=document.querySelector('#themeToggle i');
if(body.classList.contains('bg-gray-900')){
body.className='bg-gray-50 text-gray-900';
html.classList.remove('dark');
icon.className='fas fa-sun text-xl';
localStorage.setItem('theme','light');
}else{
body.className='bg-gray-900 text-white';
html.classList.add('dark');
icon.className='fas fa-moon text-xl';
localStorage.setItem('theme','dark');
}
}
function loadTheme(){
const theme=localStorage.getItem('theme')||'dark';
const body=document.body;
const html=document.documentElement;
const icon=document.querySelector('#themeToggle i');
if(theme==='light'){
body.className='bg-gray-50 text-gray-900';
html.classList.remove('dark');
icon.className='fas fa-sun text-xl';
}else{
body.className='bg-gray-900 text-white';
html.classList.add('dark');
icon.className='fas fa-moon text-xl';
}
}
const sensorConfig={RTD:{icon:'🌡️',label:'Temperature',unit:'°C',color:'text-red-500'},pH:{icon:'⚗️',label:'pH Level',unit:'',color:'text-purple-500'},EC:{icon:'⚡',label:'Conductivity',unit:'µS/cm',color:'text-cyan-500'},HUM:{icon:'💧',label:'Humidity',unit:'%',color:'text-blue-500'},DO:{icon:'🫧',label:'Dissolved Oxygen',unit:'mg/L',color:'text-teal-500'},ORP:{icon:'🔋',label:'ORP',unit:'mV',color:'text-pink-500'}};
function displaySensorValues(sensors){
const container=document.getElementById('sensor-values');
if(!sensors||Object.keys(sensors).length===0){container.innerHTML='<div class="text-gray-500 dark:text-gray-400">No sensor data available</div>';return;}
let html='';
for(const type in sensors){
const value=sensors[type];
const cfg=sensorConfig[type]||{icon:'📊',label:type,unit:'',color:'text-gray-500'};
if(typeof value==='object'&&!Array.isArray(value)){
for(const field in value){
const fieldLabel=field.replace('_',' ').replace(/\b\w/g,l=>l.toUpperCase());
const fieldValue=typeof value[field]==='number'?value[field].toFixed(2):value[field];
html+=`<div class='bg-white dark:bg-gray-700 p-4 rounded-lg border border-gray-200 dark:border-gray-600'>`;
html+=`<div class='flex items-center justify-between mb-2'>`;
html+=`<span class='text-2xl'>${cfg.icon}</span>`;
html+=`<span class='text-xs text-gray-500 dark:text-gray-400'>${type}</span>`;
html+=`</div>`;
html+=`<div class='text-sm text-gray-600 dark:text-gray-300 mb-1'>${fieldLabel}</div>`;
html+=`<div class='text-2xl font-bold ${cfg.color}'>${fieldValue}</div>`;
html+=`</div>`;
}
}else if(typeof value==='number'){
html+=`<div class='bg-white dark:bg-gray-700 p-4 rounded-lg border border-gray-200 dark:border-gray-600'>`;
html+=`<div class='flex items-center justify-between mb-2'>`;
html+=`<span class='text-2xl'>${cfg.icon}</span>`;
html+=`</div>`;
html+=`<div class='text-sm text-gray-600 dark:text-gray-300 mb-1'>${cfg.label}</div>`;
html+=`<div class='text-2xl font-bold ${cfg.color}'>${value.toFixed(2)} ${cfg.unit}</div>`;
html+=`</div>`;
}
}
container.innerHTML=html;
}
async function loadStatus(){
try{
const res=await fetch('/api/status');
if(!res.ok)throw new Error('Failed to load');
const d=await res.json();
document.getElementById('device-id').textContent=d.device_id;
document.getElementById('wifi-ssid').textContent=d.wifi_ssid;
document.getElementById('ip-addr').textContent=d.ip_address;
const upMin=Math.floor(d.uptime/60),upHr=Math.floor(upMin/60);
document.getElementById('uptime').textContent=upHr>0?`${upHr}h ${upMin%60}m`:`${upMin}m`;
document.getElementById('current-time').textContent=d.current_time;
const heapKB=(d.free_heap/1024).toFixed(1);
document.getElementById('free-heap').textContent=heapKB+' KB';
if(d.rssi){document.getElementById('wifi-rssi').textContent=d.rssi+' dBm';}
if(d.cpu_usage){document.getElementById('cpu-usage').textContent=d.cpu_usage+'%';}
if(d.sensors){displaySensorValues(d.sensors);}
document.getElementById('status-dot').className='bg-green-500 w-3 h-3 rounded-full status-dot';
document.getElementById('status-text').textContent='Device Online';
}catch(e){console.error(e);
document.getElementById('status-dot').className='bg-red-500 w-3 h-3 rounded-full';
document.getElementById('status-text').textContent='Device Offline';}}
async function testMQTT(){alert('Testing MQTT connection...');
try{const r=await fetch('/api/test-mqtt',{method:'POST'});alert('MQTT test complete');}catch(e){alert('Test failed');}}
async function rebootDevice(){if(!confirm('Reboot device now?'))return;
await fetch('/api/reboot',{method:'POST'});alert('Device rebooting...');setTimeout(()=>location.reload(),10000);}
async function clearWiFi(){if(!confirm('Clear WiFi and reset device?'))return;
await fetch('/api/clear-wifi',{method:'POST'});alert('WiFi cleared. Restarting...');setTimeout(()=>location.reload(),10000);}
async function saveSetting(){const interval=document.getElementById('mqtt-interval').value;
await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mqtt_interval:parseInt(interval)})});
alert('Settings saved!');}
async function loadSensors(){
try{const r=await fetch('/api/sensors',{signal:AbortSignal.timeout(10000)});const d=await r.json();
const list=document.getElementById('sensor-list');
if(d.count===0){list.innerHTML='<p class="text-gray-600 dark:text-gray-400">No sensors detected</p>';return;}
list.innerHTML=d.sensors.map(s=>{
let cfg=`<div class='bg-gray-50 dark:bg-gray-700 p-4 rounded-lg mb-4'>`;
cfg+=`<h3 class='text-lg font-bold text-green-600 dark:text-green-400 mb-2'>${s.type} @ 0x${s.address.toString(16).toUpperCase()}</h3>`;
if(s.firmware)cfg+=`<p class='text-sm text-gray-600 dark:text-gray-400 mb-4'>Firmware: ${s.firmware}</p>`;
if(s.type!=='MAX17048'){
cfg+=`<div class='mb-4'><label class='block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2'>Name:</label><input class='w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md bg-white dark:bg-gray-800 text-gray-900 dark:text-white' id='name-${s.address}' value='${s.name||''}'></div>`;
cfg+=`<div class='mb-3'><label class='flex items-center text-gray-700 dark:text-gray-300'><input type='checkbox' class='mr-2' id='led-${s.address}' ${s.led?'checked':''}> LED On</label></div>`;
cfg+=`<div class='mb-3'><label class='flex items-center text-gray-700 dark:text-gray-300'><input type='checkbox' class='mr-2' id='plock-${s.address}' ${s.plock?'checked':''}> Protocol Lock</label></div>`;
if(s.type==='RTD')cfg+=`<div class='mb-4'><label class='block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2'>Scale:</label><select class='w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md bg-white dark:bg-gray-800 text-gray-900 dark:text-white' id='scale-${s.address}'><option ${s.scale==='C'?'selected':''}>C</option><option ${s.scale==='F'?'selected':''}>F</option><option ${s.scale==='K'?'selected':''}>K</option></select></div>`;
if(s.type==='pH')cfg+=`<div class='mb-3'><label class='flex items-center text-gray-700 dark:text-gray-300'><input type='checkbox' class='mr-2' id='extscale-${s.address}' ${s.extended_scale?'checked':''}> Extended pH Scale</label></div>`;
if(s.type==='EC'){cfg+=`<div class='mb-4'><label class='block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2'>Probe K Value:</label><input class='w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md bg-white dark:bg-gray-800 text-gray-900 dark:text-white' type='number' step='0.1' id='probe-${s.address}' value='${s.probe_type||1.0}'></div>`;cfg+=`<div class='mb-4'><label class='block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2'>TDS Factor:</label><input class='w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md bg-white dark:bg-gray-800 text-gray-900 dark:text-white' type='number' step='0.01' id='tds-${s.address}' value='${s.tds_factor||0.5}'></div>`}
cfg+=`<button class='bg-green-600 dark:bg-green-400 hover:bg-green-700 dark:hover:bg-green-500 text-white px-4 py-2 rounded-md mt-2' onclick='saveSensorConfig(${s.address})'><i class='fas fa-save'></i> Save ${s.type} Settings</button>`;
}
cfg+=`</div>`;return cfg;}).join('');
}
catch(e){console.error('Failed to load sensors:',e);}}
async function rescanSensors(){alert('Rescanning I2C bus...');await fetch('/api/sensors/rescan',{method:'POST'});await loadSensors();alert('Rescan complete!');}
async function pauseSensors(){try{await fetch('/api/sensors/pause',{method:'POST'});alert('Sensor readings paused');}catch(e){alert('Failed to pause sensors');}}
async function resumeSensors(){try{await fetch('/api/sensors/resume',{method:'POST'});alert('Sensor readings resumed');}catch(e){alert('Failed to resume sensors');}}
async function saveSensorConfig(addr){
const cfg={address:addr};
const name=document.getElementById(`name-${addr}`)?.value;
if(name)cfg.name=name;
const led=document.getElementById(`led-${addr}`)?.checked;
if(led!==undefined)cfg.led=led;
const plock=document.getElementById(`plock-${addr}`)?.checked;
if(plock!==undefined)cfg.plock=plock;
const scale=document.getElementById(`scale-${addr}`)?.value;
if(scale)cfg.scale=scale;
const extscale=document.getElementById(`extscale-${addr}`)?.checked;
if(extscale!==undefined)cfg.extended_scale=extscale;
const probe=document.getElementById(`probe-${addr}`)?.value;
if(probe)cfg.probe_type=parseFloat(probe);
const tds=document.getElementById(`tds-${addr}`)?.value;
if(tds)cfg.tds_factor=parseFloat(tds);
await fetch('/api/sensors/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});
alert('Sensor configuration saved!');await loadSensors();}
function showTab(n){
document.querySelectorAll('.tab').forEach((t,i)=>{
if(i===n){
t.className='px-6 py-3 text-green-600 dark:text-green-400 border-b-2 border-green-600 dark:border-green-400 font-semibold tab active';
}else{
t.className='px-6 py-3 text-gray-600 dark:text-gray-400 border-b-2 border-transparent hover:text-gray-900 dark:hover:text-white tab';
}
});
document.querySelectorAll('.tab-content').forEach((c,i)=>{
c.style.display=(i===n)?'block':'none';
});
if(n===1)loadSensors();
}
let isLoadingStatus=false;
async function safeLoadStatus(){
if(isLoadingStatus)return;
isLoadingStatus=true;
try{await loadStatus();}catch(e){console.error('Status load failed:',e);}
finally{isLoadingStatus=false;}}
async function initializeDashboard(){
loadTheme();
await safeLoadStatus();
setInterval(safeLoadStatus,10000);
}
window.onload=()=>{initializeDashboard();};

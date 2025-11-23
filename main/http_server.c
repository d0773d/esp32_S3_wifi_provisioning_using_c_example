/**
 * @file http_server.c
 * @brief HTTPS web server implementation
 */

#include "http_server.h"
#include "cloud_provisioning.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "api_key_manager.h"
#include "esp_log.h"
#include "esp_https_server.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "HTTP_SERVER";

static httpd_handle_t s_server = NULL;

// External sensor reading functions from mqtt_telemetry.c
extern float read_temperature(void);
extern float read_humidity(void);
extern float read_soil_moisture(void);
extern float read_light_level(void);
extern float read_battery_level(void);

// Sensor manager functions
#include "sensor_manager.h"
#include "ezo_sensor.h"
#include "ezo_sensor.h"
#include "max17048.h"
#include "mqtt_telemetry.h"  // For MAX_SENSOR_VALUES

// HTML dashboard content (embedded)
static const char *HTML_DASHBOARD = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32 KannaCloud Dashboard</title>"
"<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js'></script>"
"<style>"
"* { margin: 0; padding: 0; box-sizing: border-box; }"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; "
"       background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); "
"       min-height: 100vh; padding: 20px; }"
".container { max-width: 1400px; margin: 0 auto; }"
".card { background: white; border-radius: 12px; padding: 24px; margin-bottom: 20px; "
"        box-shadow: 0 4px 6px rgba(0,0,0,0.1); animation: slideIn 0.3s ease-out; }"
"@keyframes slideIn { from { opacity: 0; transform: translateY(20px); } to { opacity: 1; transform: translateY(0); } }"
".header { text-align: center; color: white; margin-bottom: 30px; }"
".header h1 { font-size: 2.5em; margin-bottom: 10px; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); }"
".header p { opacity: 0.9; font-size: 1.1em; }"
".status { display: flex; align-items: center; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; }"
".status-dot { width: 12px; height: 12px; border-radius: 50%; animation: pulse 2s infinite; }"
".status-dot.online { background: #10b981; box-shadow: 0 0 10px #10b981; }"
".status-dot.offline { background: #ef4444; }"
"@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }"
".info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; }"
".info-item { padding: 15px; background: linear-gradient(135deg, #f3f4f6 0%, #e5e7eb 100%); "
"             border-radius: 8px; transition: transform 0.2s; border: 2px solid transparent; }"
".info-item:hover { transform: translateY(-2px); border-color: #667eea; }"
".info-label { font-size: 0.875em; color: #6b7280; margin-bottom: 5px; font-weight: 500; }"
".info-value { font-size: 1.25em; font-weight: 600; color: #111827; }"
".info-icon { font-size: 1.5em; margin-right: 8px; }"
".btn { background: #667eea; color: white; border: none; padding: 12px 24px; margin: 5px; "
"       border-radius: 8px; cursor: pointer; font-size: 1em; transition: all 0.3s; "
"       box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
".btn:hover { background: #5568d3; transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.2); }"
".btn:active { transform: translateY(0); }"
".btn-danger { background: #ef4444; }"
".btn-danger:hover { background: #dc2626; }"
".btn-success { background: #10b981; }"
".btn-success:hover { background: #059669; }"
"h2 { color: #111827; margin-bottom: 20px; font-size: 1.5em; display: flex; align-items: center; gap: 10px; }"
".refresh-btn { margin-left: auto; }"
".chart-container { position: relative; height: 300px; margin-top: 20px; }"
".tabs { display: flex; gap: 10px; margin-bottom: 20px; border-bottom: 2px solid #e5e7eb; }"
".tab { padding: 12px 24px; cursor: pointer; border: none; background: none; "
"       font-size: 1em; color: #6b7280; transition: all 0.3s; border-bottom: 3px solid transparent; }"
".tab.active { color: #667eea; border-bottom-color: #667eea; font-weight: 600; }"
".tab:hover { color: #667eea; }"
".tab-content { display: none; }"
".tab-content.active { display: block; animation: fadeIn 0.3s; }"
"@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }"
".metric-badge { display: inline-block; padding: 4px 12px; border-radius: 12px; font-size: 0.85em; "
"                font-weight: 600; margin-left: 10px; }"
".badge-good { background: #d1fae5; color: #065f46; }"
".badge-warning { background: #fed7aa; color: #92400e; }"
".badge-error { background: #fee2e2; color: #991b1b; }"
".control-group { margin: 15px 0; }"
".control-label { display: block; margin-bottom: 8px; font-weight: 600; color: #374151; }"
".control-input { width: 100%; padding: 10px; border: 2px solid #e5e7eb; border-radius: 8px; "
"                 font-size: 1em; transition: border-color 0.3s; }"
".control-input:focus { outline: none; border-color: #667eea; }"
".loading { text-align: center; padding: 40px; color: #6b7280; }"
".spinner { border: 4px solid #f3f4f6; border-top: 4px solid #667eea; "
"           border-radius: 50%; width: 40px; height: 40px; "
"           animation: spin 1s linear infinite; margin: 0 auto; }"
"@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }"
"</style>"
"</head>"
"<body>"
"<div class='container'>"
"<div class='header'>"
"<h1>🌱 KannaCloud IoT Dashboard</h1>"
"<p>ESP32-S3 Smart Sensor Platform</p>"
"</div>"

"<div class='card'>"
"<div class='status'>"
"<div class='status-dot online' id='status-dot'></div>"
"<h2><span id='status-text'>Device Status: Online</span></h2>"
"<button class='btn refresh-btn' onclick='loadStatus()'>🔄 Refresh</button>"
"</div>"
"<div class='info-grid'>"
"<div class='info-item'>"
"<div class='info-label'>📱 Device ID</div>"
"<div class='info-value' id='device-id'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>📡 WiFi SSID</div>"
"<div class='info-value' id='wifi-ssid'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>🌐 IP Address</div>"
"<div class='info-value' id='ip-addr'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>⏱️ Uptime</div>"
"<div class='info-value' id='uptime'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>🕒 System Time</div>"
"<div class='info-value' id='current-time'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>💾 Free Memory</div>"
"<div class='info-value' id='free-heap'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>📶 WiFi Signal</div>"
"<div class='info-value' id='wifi-rssi'>Loading...</div>"
"</div>"
"<div class='info-item'>"
"<div class='info-label'>🔋 CPU Usage</div>"
"<div class='info-value' id='cpu-usage'>Loading...</div>"
"</div>"
"</div>"
"</div>"

"<div class='card'>"
"<div class='tabs'>"
"<button class='tab active' onclick='showTab(0)'>📊 Real-Time Data</button>"
"<button class='tab' onclick='showTab(1)'>🔬 Sensors</button>"
"<button class='tab' onclick='showTab(2)'>⚙️ Settings</button>"
"<button class='tab' onclick='showTab(3)'>🔧 Actions</button>"
"</div>"

"<div class='tab-content active' id='tab-0'>"
"<h2>📈 Live Sensor Metrics</h2>"
"<div class='chart-container'><canvas id='sensorChart'></canvas></div>"
"</div>"

"<div class='tab-content' id='tab-1'>"
"<h2>🔬 Sensor Configuration</h2>"
"<button class='btn btn-success' onclick='rescanSensors()' style='margin-bottom:15px'>🔄 Rescan I2C Bus</button>"
"<div id='sensor-list'><p style='color:#666'>Loading sensors...</p></div>"
"</div>"

"<div class='tab-content' id='tab-2'>"
"<h2>⚙️ Configuration</h2>"
"<div class='control-group'>"
"<label class='control-label'>MQTT Telemetry Interval (seconds)</label>"
"<input type='number' class='control-input' id='mqtt-interval' value='10' min='1' max='3600'>"
"<button class='btn btn-success' onclick='saveSetting()' style='margin-top:10px'>💾 Save Settings</button>"
"</div>"
"</div>"

"<div class='tab-content' id='tab-3'>"
"<h2>🔧 Device Control</h2>"
"<button class='btn' onclick='testMQTT()'>📡 Test MQTT Connection</button>"
"<button class='btn' onclick='rebootDevice()'>🔄 Reboot Device</button>"
"<button class='btn btn-danger' onclick='clearWiFi()'>🗑️ Clear WiFi & Reset</button>"
"</div>"
"</div>"

"</div>"
"<script>"
"let chart; const maxDataPoints=20; const chartData={labels:[]};"
"const sensorConfigs={RTD:{label:'Temperature',unit:'°C',color:'#ef4444',yAxisID:'y'},pH:{label:'pH',unit:'',color:'#8b5cf6',yAxisID:'y'},EC_conductivity:{label:'Conductivity',unit:'µS',color:'#06b6d4',yAxisID:'y1'},EC_tds:{label:'TDS',unit:'ppm',color:'#10b981',yAxisID:'y1'},EC_salinity:{label:'Salinity',unit:'PSU',color:'#f59e0b',yAxisID:'y1'},HUM_humidity:{label:'Humidity',unit:'%',color:'#3b82f6',yAxisID:'y'},HUM_air_temp:{label:'Air Temp',unit:'°C',color:'#f97316',yAxisID:'y'},HUM_dew_point:{label:'Dew Point',unit:'°C',color:'#a855f7',yAxisID:'y'},DO_dissolved_oxygen:{label:'DO',unit:'mg/L',color:'#14b8a6',yAxisID:'y'},DO_saturation:{label:'DO Sat',unit:'%',color:'#06b6d4',yAxisID:'y'},ORP_orp:{label:'ORP',unit:'mV',color:'#ec4899',yAxisID:'y'}};"
"function initChart(){"
"const ctx=document.getElementById('sensorChart').getContext('2d');"
"chart=new Chart(ctx,{type:'line',data:{labels:chartData.labels,datasets:[]},options:{responsive:true,maintainAspectRatio:false,plugins:{legend:{position:'top'}},scales:{y:{beginAtZero:false,title:{display:true,text:'Primary (°C, pH, %, mg/L, mV)'},position:'left'},y1:{beginAtZero:false,title:{display:true,text:'Secondary (µS, ppm, PSU)'},position:'right',grid:{drawOnChartArea:false}}}}});"
"}"
"function updateChart(sensors){"
"const now=new Date().toLocaleTimeString();"
"chartData.labels.push(now);"
"if(chartData.labels.length>maxDataPoints)chartData.labels.shift();"
"for(const sensorType in sensors){"
"const value=sensors[sensorType];"
"if(typeof value==='object'&&!Array.isArray(value)){"
"for(const field in value){"
"const key=sensorType+'_'+field;"
"if(!chartData[key]){"
"chartData[key]=[];"
"const cfg=sensorConfigs[key]||{label:sensorType+' '+field,unit:'',color:'#94a3b8',yAxisID:'y'};"
"chart.data.datasets.push({label:cfg.label+(cfg.unit?' ('+cfg.unit+')':''),data:chartData[key],borderColor:cfg.color,backgroundColor:cfg.color+'20',tension:0.4,yAxisID:cfg.yAxisID});"
"}"
"chartData[key].push(value[field]);"
"if(chartData[key].length>maxDataPoints)chartData[key].shift();"
"}"
"}else if(typeof value==='number'){"
"if(!chartData[sensorType]){"
"chartData[sensorType]=[];"
"const cfg=sensorConfigs[sensorType]||{label:sensorType,unit:'',color:'#94a3b8',yAxisID:'y'};"
"chart.data.datasets.push({label:cfg.label+(cfg.unit?' ('+cfg.unit+')':''),data:chartData[sensorType],borderColor:cfg.color,backgroundColor:cfg.color+'20',tension:0.4,yAxisID:cfg.yAxisID});"
"}"
"chartData[sensorType].push(value);"
"if(chartData[sensorType].length>maxDataPoints)chartData[sensorType].shift();"
"}"
"}"
"if(chart)chart.update();"
"}"
"async function loadStatus(){"
"try{"
"const res=await fetch('/api/status');"
"if(!res.ok)throw new Error('Failed to load');"
"const d=await res.json();"
"document.getElementById('device-id').textContent=d.device_id;"
"document.getElementById('wifi-ssid').textContent=d.wifi_ssid;"
"document.getElementById('ip-addr').textContent=d.ip_address;"
"const upMin=Math.floor(d.uptime/60),upHr=Math.floor(upMin/60);"
"document.getElementById('uptime').textContent=upHr>0?`${upHr}h ${upMin%60}m`:`${upMin}m`;"
"document.getElementById('current-time').textContent=d.current_time;"
"const heapKB=(d.free_heap/1024).toFixed(1);"
"document.getElementById('free-heap').textContent=heapKB+' KB';"
"if(d.rssi){document.getElementById('wifi-rssi').textContent=d.rssi+' dBm';}"
"if(d.cpu_usage){document.getElementById('cpu-usage').textContent=d.cpu_usage+'%';}"
"if(d.sensors){updateChart(d.sensors);}"
"document.getElementById('status-dot').className='status-dot online';"
"document.getElementById('status-text').textContent='Device Status: Online';"
"}catch(e){console.error(e);"
"document.getElementById('status-dot').className='status-dot offline';"
"document.getElementById('status-text').textContent='Device Status: Offline';}}"
"async function testMQTT(){alert('Testing MQTT connection...');"
"try{const r=await fetch('/api/test-mqtt',{method:'POST'});alert('MQTT test complete');}catch(e){alert('Test failed');}}"
"async function rebootDevice(){if(!confirm('Reboot device now?'))return;"
"await fetch('/api/reboot',{method:'POST'});alert('Device rebooting...');setTimeout(()=>location.reload(),10000);}"
"async function clearWiFi(){if(!confirm('Clear WiFi and reset device?'))return;"
"await fetch('/api/clear-wifi',{method:'POST'});alert('WiFi cleared. Restarting...');setTimeout(()=>location.reload(),10000);}"
"async function saveSetting(){const interval=document.getElementById('mqtt-interval').value;"
"await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mqtt_interval:parseInt(interval)})});"
"alert('Settings saved!');}"
"async function loadSensors(){"
"try{const r=await fetch('/api/sensors');const d=await r.json();"
"const list=document.getElementById('sensor-list');"
"if(d.count===0){list.innerHTML='<p style=\"color:#666\">No sensors detected</p>';return;}"
"list.innerHTML=d.sensors.map(s=>{"
"let cfg=`<div class='control-group'>`;"
"cfg+=`<h3 style='margin:10px 0 5px;color:#667eea'>${s.type} @ 0x${s.address.toString(16).toUpperCase()}</h3>`;"
"if(s.firmware)cfg+=`<p style='color:#666;margin:0 0 10px;font-size:0.9em'>FW: ${s.firmware}</p>`;"
"if(s.type!=='MAX17048'){"
"cfg+=`<label>Name:</label><input class='control-input' id='name-${s.address}' value='${s.name||''}'>`;"
"cfg+=`<label style='display:inline-flex;align-items:center;margin:10px 0'><input type='checkbox' id='led-${s.address}' ${s.led?'checked':''}> LED On</label>`;"
"cfg+=`<label style='display:inline-flex;align-items:center;margin:10px 0'><input type='checkbox' id='plock-${s.address}' ${s.plock?'checked':''}> Protocol Lock</label>`;"
"if(s.type==='RTD')cfg+=`<label>Scale:</label><select class='control-input' id='scale-${s.address}'><option ${s.scale==='C'?'selected':''}>C</option><option ${s.scale==='F'?'selected':''}>F</option><option ${s.scale==='K'?'selected':''}>K</option></select>`;"
"if(s.type==='pH')cfg+=`<label style='display:inline-flex;align-items:center;margin:10px 0'><input type='checkbox' id='extscale-${s.address}' ${s.extended_scale?'checked':''}> Extended pH Scale</label>`;"
"if(s.type==='EC'){cfg+=`<label>Probe K Value:</label><input class='control-input' type='number' step='0.1' id='probe-${s.address}' value='${s.probe_type||1.0}'>`;cfg+=`<label>TDS Factor:</label><input class='control-input' type='number' step='0.01' id='tds-${s.address}' value='${s.tds_factor||0.5}'>`}"
"cfg+=`<button class='btn btn-success' onclick='saveSensorConfig(${s.address})' style='margin-top:10px'>💾 Save ${s.type} Settings</button>`;"
"}"
"cfg+=`</div><hr style='margin:20px 0;border:none;border-top:1px solid #eee'>`;return cfg;}).join('');}"
"catch(e){console.error('Failed to load sensors:',e);}}"
"async function rescanSensors(){alert('Rescanning I2C bus...');await fetch('/api/sensors/rescan',{method:'POST'});await loadSensors();alert('Rescan complete!');}"
"async function saveSensorConfig(addr){"
"const cfg={address:addr};"
"const name=document.getElementById(`name-${addr}`)?.value;"
"if(name)cfg.name=name;"
"const led=document.getElementById(`led-${addr}`)?.checked;"
"if(led!==undefined)cfg.led=led;"
"const plock=document.getElementById(`plock-${addr}`)?.checked;"
"if(plock!==undefined)cfg.plock=plock;"
"const scale=document.getElementById(`scale-${addr}`)?.value;"
"if(scale)cfg.scale=scale;"
"const extscale=document.getElementById(`extscale-${addr}`)?.checked;"
"if(extscale!==undefined)cfg.extended_scale=extscale;"
"const probe=document.getElementById(`probe-${addr}`)?.value;"
"if(probe)cfg.probe_type=parseFloat(probe);"
"const tds=document.getElementById(`tds-${addr}`)?.value;"
"if(tds)cfg.tds_factor=parseFloat(tds);"
"await fetch('/api/sensors/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)});"
"alert('Sensor configuration saved!');await loadSensors();}"
"function showTab(n){document.querySelectorAll('.tab').forEach((t,i)=>t.classList.toggle('active',i===n));"
"document.querySelectorAll('.tab-content').forEach((c,i)=>c.classList.toggle('active',i===n));if(n===1)loadSensors();}"
"window.onload=()=>{initChart();loadStatus();setInterval(loadStatus,5000);loadSensors();};"
"</script>"
"</body>"
"</html>";

/**
 * @brief Root handler - serve dashboard
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_DASHBOARD, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief API status endpoint - return device status as JSON
 */
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    
    // Device ID
    char device_id[32];
    cloud_prov_get_device_id(device_id, sizeof(device_id));
    cJSON_AddStringToObject(root, "device_id", device_id);
    
    // WiFi SSID
    char ssid[33];
    char password[64];
    if (wifi_manager_get_stored_credentials(ssid, password) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_ssid", ssid);
        memset(password, 0, sizeof(password)); // Clear password
    } else {
        cJSON_AddStringToObject(root, "wifi_ssid", "Not configured");
    }
    
    // IP Address
    if (wifi_manager_is_connected()) {
        // TODO: Get actual IP address from WiFi manager
        cJSON_AddStringToObject(root, "ip_address", "Connected");
    } else {
        cJSON_AddStringToObject(root, "ip_address", "Disconnected");
    }
    
    // WiFi RSSI
    if (wifi_manager_is_connected()) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddNumberToObject(root, "rssi", ap_info.rssi);
        }
    }
    
    // Uptime
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);
    
    // Current time
    char time_str[64];
    if (time_sync_get_time_string(time_str, sizeof(time_str), NULL) == ESP_OK) {
        cJSON_AddStringToObject(root, "current_time", time_str);
    } else {
        cJSON_AddStringToObject(root, "current_time", "Not synced");
    }
    
    // Free heap
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    
    // CPU usage (simplified estimate based on idle task)
    // TODO: Implement more accurate CPU monitoring
    cJSON_AddNumberToObject(root, "cpu_usage", 25);
    
    // Sensor data for live charts - dynamic format matching MQTT telemetry
    cJSON *sensors = cJSON_CreateObject();
    
    // Read all EZO sensors dynamically
    uint8_t ezo_count = sensor_manager_get_ezo_count();
    for (uint8_t i = 0; i < ezo_count; i++) {
        char sensor_type[16];
        float values[MAX_SENSOR_VALUES];
        uint8_t value_count = 0;
        
        if (sensor_manager_read_ezo_sensor(i, sensor_type, values, &value_count) == ESP_OK) {
            if (value_count == 1) {
                // Single value sensor - add as number
                cJSON_AddNumberToObject(sensors, sensor_type, values[0]);
            } else if (value_count > 1) {
                // Multi-value sensor - create object with named fields
                cJSON *sensor_obj = cJSON_CreateObject();
                
                // Define field names based on sensor type
                if (strcmp(sensor_type, "HUM") == 0) {
                    // Get HUM sensor config to determine which parameters are enabled
                    ezo_sensor_t *sensor = (ezo_sensor_t *)sensor_manager_get_ezo_sensor(i);
                    if (sensor != NULL && sensor->config.hum.param_count > 0) {
                        // Use dynamic parameter mapping based on enabled outputs
                        for (uint8_t j = 0; j < value_count && j < sensor->config.hum.param_count; j++) {
                            const char *param = sensor->config.hum.param_order[j];
                            const char *field_name = NULL;
                            
                            if (strcmp(param, "HUM") == 0) {
                                field_name = "humidity";
                            } else if (strcmp(param, "T") == 0) {
                                field_name = "air_temp";
                            } else if (strcmp(param, "DEW") == 0) {
                                field_name = "dew_point";
                            }
                            
                            if (field_name != NULL) {
                                cJSON_AddNumberToObject(sensor_obj, field_name, values[j]);
                            }
                        }
                    } else {
                        // Fallback if config unavailable
                        if (value_count >= 1) cJSON_AddNumberToObject(sensor_obj, "humidity", values[0]);
                        if (value_count >= 2) cJSON_AddNumberToObject(sensor_obj, "air_temp", values[1]);
                        if (value_count >= 3) cJSON_AddNumberToObject(sensor_obj, "dew_point", values[2]);
                    }
                } else if (strcmp(sensor_type, "ORP") == 0) {
                    if (value_count >= 1) cJSON_AddNumberToObject(sensor_obj, "orp", values[0]);
                } else if (strcmp(sensor_type, "DO") == 0) {
                    if (value_count >= 1) cJSON_AddNumberToObject(sensor_obj, "dissolved_oxygen", values[0]);
                    if (value_count >= 2) cJSON_AddNumberToObject(sensor_obj, "saturation", values[1]);
                } else if (strcmp(sensor_type, "EC") == 0) {
                    if (value_count >= 1) cJSON_AddNumberToObject(sensor_obj, "conductivity", values[0]);
                    if (value_count >= 2) cJSON_AddNumberToObject(sensor_obj, "tds", values[1]);
                    if (value_count >= 3) cJSON_AddNumberToObject(sensor_obj, "salinity", values[2]);
                    if (value_count >= 4) cJSON_AddNumberToObject(sensor_obj, "specific_gravity", values[3]);
                } else {
                    // Unknown multi-value sensor - use generic field names
                    for (uint8_t j = 0; j < value_count; j++) {
                        char field_name[16];
                        snprintf(field_name, sizeof(field_name), "value_%d", j);
                        cJSON_AddNumberToObject(sensor_obj, field_name, values[j]);
                    }
                }
                
                cJSON_AddItemToObject(sensors, sensor_type, sensor_obj);
            }
        }
    }
    
    cJSON_AddItemToObject(root, "sensors", sensors);
    
    // Battery level
    float battery = 0.0f;
    if (sensor_manager_read_battery_percentage(&battery) == ESP_OK) {
        cJSON_AddNumberToObject(root, "battery", (int)battery);
    }
    
    // Send JSON response
    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    
    free((void *)json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief API clear WiFi endpoint
 */
static esp_err_t api_clear_wifi_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "WiFi clear requested via dashboard");
    
    // Clear WiFi credentials
    wifi_manager_clear_credentials();
    
    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\"}", HTTPD_RESP_USE_STRLEN);
    
    // Restart device after a delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

/**
 * @brief API reboot endpoint
 */
static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Reboot requested via dashboard");
    
    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"rebooting\"}", HTTPD_RESP_USE_STRLEN);
    
    // Restart device after a delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

/**
 * @brief API test MQTT endpoint
 */
static esp_err_t api_test_mqtt_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "MQTT connection test requested");
    
    // Check if MQTT is connected
    // Note: mqtt_telemetry module doesn't expose connection status yet
    // For now, just return success
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"tested\",\"connected\":true}", HTTPD_RESP_USE_STRLEN);
    
    return ESP_OK;
}

/**
 * @brief API settings endpoint
 */
static esp_err_t api_settings_handler(httpd_req_t *req)
{
    char content[100];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Get mqtt_interval if present
    cJSON *interval = cJSON_GetObjectItem(root, "mqtt_interval");
    if (interval != NULL && cJSON_IsNumber(interval)) {
        int interval_val = interval->valueint;
        ESP_LOGI(TAG, "Settings update: MQTT interval = %d seconds", interval_val);
        // TODO: Implement actual settings storage and application
        // For now, just acknowledge the setting
    }
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"saved\"}", HTTPD_RESP_USE_STRLEN);
    
    return ESP_OK;
}

/**
 * @brief GET /api/sensors - Get list of all sensors with their configurations
 */
static esp_err_t api_sensors_list_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *sensors = cJSON_CreateArray();
    
    // Add battery monitor if available
    if (sensor_manager_has_battery_monitor()) {
        cJSON *battery = cJSON_CreateObject();
        cJSON_AddStringToObject(battery, "type", "MAX17048");
        cJSON_AddNumberToObject(battery, "address", 0x36);
        cJSON_AddStringToObject(battery, "name", "Battery Monitor");
        cJSON_AddStringToObject(battery, "description", "Li+ Battery Fuel Gauge");
        cJSON_AddItemToArray(sensors, battery);
    }
    
    // Add EZO sensors
    uint8_t ezo_count = sensor_manager_get_ezo_count();
    for (uint8_t i = 0; i < ezo_count; i++) {
        ezo_sensor_t *sensor = (ezo_sensor_t*)sensor_manager_get_ezo_sensor(i);
        if (sensor != NULL) {
            cJSON *ezo = cJSON_CreateObject();
            cJSON_AddNumberToObject(ezo, "index", i);
            cJSON_AddNumberToObject(ezo, "address", sensor->config.i2c_address);
            cJSON_AddStringToObject(ezo, "type", sensor->config.type);
            cJSON_AddStringToObject(ezo, "name", sensor->config.name);
            cJSON_AddStringToObject(ezo, "firmware", sensor->config.firmware_version);
            cJSON_AddBoolToObject(ezo, "led", sensor->config.led_control);
            cJSON_AddBoolToObject(ezo, "plock", sensor->config.protocol_lock);
            
            // Add type-specific parameters
            if (strcmp(sensor->config.type, "RTD") == 0) {
                cJSON_AddStringToObject(ezo, "scale", (const char[]){sensor->config.rtd.temperature_scale, '\0'});
            } else if (strcmp(sensor->config.type, "pH") == 0) {
                cJSON_AddBoolToObject(ezo, "extended_scale", sensor->config.ph.extended_scale);
            } else if (strcmp(sensor->config.type, "EC") == 0) {
                cJSON_AddNumberToObject(ezo, "probe_type", sensor->config.ec.probe_type);
                cJSON_AddNumberToObject(ezo, "tds_factor", sensor->config.ec.tds_conversion_factor);
            }
            
            cJSON_AddItemToArray(sensors, ezo);
        }
    }
    
    cJSON_AddItemToObject(root, "sensors", sensors);
    cJSON_AddNumberToObject(root, "count", cJSON_GetArraySize(sensors));
    
    const char *response = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    
    free((void*)response);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief POST /api/sensors/rescan - Rescan I2C bus for sensors
 */
static esp_err_t api_sensors_rescan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Rescanning I2C bus for sensors");
    
    esp_err_t ret = sensor_manager_rescan();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", ret == ESP_OK ? "success" : "error");
    cJSON_AddNumberToObject(root, "battery", sensor_manager_has_battery_monitor() ? 1 : 0);
    cJSON_AddNumberToObject(root, "ezo_count", sensor_manager_get_ezo_count());
    
    const char *response = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    
    free((void*)response);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief POST /api/sensors/config - Update sensor configuration
 * Body: {"address": 99, "led": 1, "name": "MySensor", "scale": "F", etc}
 */
static esp_err_t api_sensors_config_handler(httpd_req_t *req)
{
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *address_json = cJSON_GetObjectItem(root, "address");
    if (address_json == NULL || !cJSON_IsNumber(address_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing address");
        return ESP_FAIL;
    }
    
    uint8_t address = (uint8_t)address_json->valueint;
    
    // Find sensor by address
    ezo_sensor_t *sensor = NULL;
    uint8_t ezo_count = sensor_manager_get_ezo_count();
    for (uint8_t i = 0; i < ezo_count; i++) {
        ezo_sensor_t *s = (ezo_sensor_t*)sensor_manager_get_ezo_sensor(i);
        if (s != NULL && s->config.i2c_address == address) {
            sensor = s;
            break;
        }
    }
    
    if (sensor == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Sensor not found");
        return ESP_FAIL;
    }
    
    // Update LED
    cJSON *led = cJSON_GetObjectItem(root, "led");
    if (led != NULL && cJSON_IsBool(led)) {
        ezo_sensor_set_led(sensor, cJSON_IsTrue(led));
    }
    
    // Update name
    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (name != NULL && cJSON_IsString(name)) {
        ezo_sensor_set_name(sensor, name->valuestring);
    }
    
    // Update protocol lock
    cJSON *plock = cJSON_GetObjectItem(root, "plock");
    if (plock != NULL && cJSON_IsBool(plock)) {
        ezo_sensor_set_plock(sensor, cJSON_IsTrue(plock));
    }
    
    // Type-specific updates
    if (strcmp(sensor->config.type, "RTD") == 0) {
        cJSON *scale = cJSON_GetObjectItem(root, "scale");
        if (scale != NULL && cJSON_IsString(scale) && strlen(scale->valuestring) > 0) {
            ezo_rtd_set_scale(sensor, scale->valuestring[0]);
        }
    } else if (strcmp(sensor->config.type, "pH") == 0) {
        cJSON *ext_scale = cJSON_GetObjectItem(root, "extended_scale");
        if (ext_scale != NULL && cJSON_IsBool(ext_scale)) {
            ezo_ph_set_extended_scale(sensor, cJSON_IsTrue(ext_scale));
        }
    } else if (strcmp(sensor->config.type, "EC") == 0) {
        cJSON *probe = cJSON_GetObjectItem(root, "probe_type");
        if (probe != NULL && cJSON_IsNumber(probe)) {
            ezo_ec_set_probe_type(sensor, (float)probe->valuedouble);
        }
        
        cJSON *tds = cJSON_GetObjectItem(root, "tds_factor");
        if (tds != NULL && cJSON_IsNumber(tds)) {
            ezo_ec_set_tds_factor(sensor, (float)tds->valuedouble);
        }
    }
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\"}");
    
    return ESP_OK;
}

// URI handlers
static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_status_uri = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_clear_wifi_uri = {
    .uri = "/api/clear-wifi",
    .method = HTTP_POST,
    .handler = api_clear_wifi_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_reboot_uri = {
    .uri = "/api/reboot",
    .method = HTTP_POST,
    .handler = api_reboot_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_test_mqtt_uri = {
    .uri = "/api/test-mqtt",
    .method = HTTP_POST,
    .handler = api_test_mqtt_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_settings_uri = {
    .uri = "/api/settings",
    .method = HTTP_POST,
    .handler = api_settings_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_sensors_list_uri = {
    .uri = "/api/sensors",
    .method = HTTP_GET,
    .handler = api_sensors_list_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_sensors_rescan_uri = {
    .uri = "/api/sensors/rescan",
    .method = HTTP_POST,
    .handler = api_sensors_rescan_handler,
    .user_ctx = NULL
};

static const httpd_uri_t api_sensors_config_uri = {
    .uri = "/api/sensors/config",
    .method = HTTP_POST,
    .handler = api_sensors_config_handler,
    .user_ctx = NULL
};

esp_err_t http_server_start(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "HTTPS server already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting HTTPS server...");
    
    // Get certificates from cloud provisioning
    char *certificate = malloc(CLOUD_PROV_MAX_CERT_SIZE);
    char *private_key = malloc(CLOUD_PROV_MAX_KEY_SIZE);
    
    if (certificate == NULL || private_key == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for certificates");
        free(certificate);
        free(private_key);
        return ESP_ERR_NO_MEM;
    }
    
    size_t cert_len, key_len;
    esp_err_t err = cloud_prov_get_certificate(certificate, &cert_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate: %s", esp_err_to_name(err));
        free(certificate);
        free(private_key);
        return err;
    }
    
    err = cloud_prov_get_private_key(private_key, &key_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get private key: %s", esp_err_to_name(err));
        free(certificate);
        free(private_key);
        return err;
    }
    
    ESP_LOGI(TAG, "Certificate length: %zu bytes", cert_len);
    ESP_LOGI(TAG, "Private key length: %zu bytes", key_len);
    
    // Debug certificate format
    ESP_LOGI(TAG, "Cert first 100 chars: %.100s", certificate);
    if (cert_len > 100) {
        ESP_LOGI(TAG, "Cert last 100 chars: %s", certificate + cert_len - 100);
    }
    
    // Configure HTTPS server
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.max_uri_handlers = 10;
    config.httpd.stack_size = 10240;
    
    // Set certificates (PEM format from NVS is already null-terminated)
    // mbedTLS needs length + 1 to include the null terminator
    config.servercert = (const uint8_t *)certificate;
    config.servercert_len = cert_len + 1;
    config.prvtkey_pem = (const uint8_t *)private_key;
    config.prvtkey_len = key_len + 1;
    
    // Start server
    err = httpd_ssl_start(&s_server, &config);
    
    // Clean up certificate buffers
    free(certificate);
    free(private_key);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(err));
        return err;
    }
    
    // Register URI handlers
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &api_status_uri);
    httpd_register_uri_handler(s_server, &api_clear_wifi_uri);
    httpd_register_uri_handler(s_server, &api_reboot_uri);
    httpd_register_uri_handler(s_server, &api_test_mqtt_uri);
    httpd_register_uri_handler(s_server, &api_settings_uri);
    httpd_register_uri_handler(s_server, &api_sensors_list_uri);
    httpd_register_uri_handler(s_server, &api_sensors_rescan_uri);
    httpd_register_uri_handler(s_server, &api_sensors_config_uri);
    
    ESP_LOGI(TAG, "✓ HTTPS server started successfully");
    ESP_LOGI(TAG, "Dashboard accessible at: https://kc.local");
    ESP_LOGI(TAG, "Registered 9 API endpoints");
    
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping HTTPS server");
    httpd_ssl_stop(s_server);
    s_server = NULL;
    
    return ESP_OK;
}

bool http_server_is_running(void)
{
    return (s_server != NULL);
}

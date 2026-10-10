// web_ui.cpp
#include "web_ui.h"
#include "config.h"
#include "mega_protocol.h"
#include "github_ota.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

// HTML хранится в flash (raw literal), не тратит RAM
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%DEVICE_NAME% — MeteoLink</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--border:#30363d;--accent:#58a6ff;
      --text:#c9d1d9;--dim:#8b949e;--ok:#3fb950;--bad:#f85149;--warn:#d29922}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;
     background:var(--bg);color:var(--text);padding:16px;min-height:100vh}
h1{font-size:20px;margin-bottom:14px;color:#fff;display:flex;align-items:center;gap:8px}
h1 small{font-size:12px;color:var(--dim);font-weight:400}
.tabs{display:flex;gap:4px;margin-bottom:16px;background:var(--card);
      padding:4px;border-radius:10px;border:1px solid var(--border)}
.tab{flex:1;padding:9px 6px;text-align:center;cursor:pointer;border-radius:8px;
     font-weight:600;font-size:12px;color:var(--dim);background:none;border:none;
     transition:all .15s}
.tab:hover{color:var(--text)}
.tab.active{background:var(--accent);color:#fff}
.content{display:none}
.content.active{display:block}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;
      padding:14px;margin-bottom:12px}
.card h2{font-size:13px;color:var(--dim);margin-bottom:10px;text-transform:uppercase;
         letter-spacing:.5px;font-weight:600}
.sensors{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.sensor{text-align:center;padding:14px 6px;background:var(--bg);border-radius:10px;
        border:1px solid var(--border)}
.sensor .val{font-size:26px;font-weight:700;color:#fff;line-height:1.1}
.sensor .unit{font-size:11px;color:var(--dim);margin-top:2px}
.sensor .lbl{font-size:11px;color:var(--dim);margin-top:6px}
.row{display:flex;justify-content:space-between;align-items:center;
     padding:8px 0;border-bottom:1px solid var(--border);font-size:13px}
.row:last-child{border-bottom:none}
.row .lbl{color:var(--dim)}
.row .val{font-weight:600}
.status{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;
        border-radius:20px;font-size:12px;font-weight:600}
.status.on{background:rgba(63,185,80,.15);color:var(--ok)}
.status.off{background:rgba(248,81,73,.15);color:var(--bad)}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor}
.field{margin-bottom:10px}
.field label{display:block;font-size:12px;color:var(--dim);margin-bottom:4px}
.field input,.field select{width:100%;padding:9px;border-radius:8px;
    border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:14px}
.btn{padding:10px 18px;border-radius:8px;border:none;font-weight:600;
     cursor:pointer;font-size:13px;margin-top:10px}
.btn-p{background:var(--accent);color:#fff}
.btn-w{background:var(--warn);color:#000}
.btn-d{background:var(--bad);color:#fff}
.mini{display:flex;gap:8px}
.mini>*{flex:1}
.log{background:#000;border:1px solid var(--border);border-radius:8px;
     padding:10px;font-family:'Courier New',monospace;font-size:11px;
     color:#0f0;max-height:420px;overflow-y:auto;white-space:pre-wrap;
     word-break:break-all}
.note{font-size:12px;color:var(--dim);margin-top:8px}
</style>
</head>
<body>
<h1>🌤️ %DEVICE_NAME% <small>%DEVICE_LOCATION%</small></h1>

<div class="tabs">
  <button class="tab active" data-tab="dash">📊 Дашборд</button>
  <button class="tab" data-tab="set">⚙️ Настройки</button>
  <button class="tab" data-tab="ota">🔄 OTA</button>
  <button class="tab" data-tab="dbg">🐛 Debug</button>
</div>

<!-- DASHBOARD -->
<div id="dash" class="content active">
  <div class="card">
    <h2>Показания датчиков</h2>
    <div class="sensors">
      <div class="sensor"><div class="val" id="t">—</div><div class="unit">°C</div><div class="lbl">Температура</div></div>
      <div class="sensor"><div class="val" id="h">—</div><div class="unit">%</div><div class="lbl">Влажность</div></div>
      <div class="sensor"><div class="val" id="p">—</div><div class="unit">мм</div><div class="lbl">Давление</div></div>
    </div>
  </div>
  <div class="card">
    <h2>Состояние</h2>
    <div class="row"><span class="lbl">Mega</span><span id="mega" class="status off"><span class="dot"></span>Offline</span></div>
    <div class="row"><span class="lbl">Версия ESP</span><span class="val" id="vesp">—</span></div>
    <div class="row"><span class="lbl">Обновлено</span><span class="val" id="age">—</span></div>
    <div class="row"><span class="lbl">RSSI Wi-Fi</span><span class="val" id="rssi">—</span></div>
    <div class="row"><span class="lbl">Uptime</span><span class="val" id="up">—</span></div>
  </div>
</div>

<!-- SETTINGS -->
<div id="set" class="content">
  <div class="card">
    <h2>Регион</h2>
    <div class="field">
      <label>Часовой пояс (IANA)</label>
      <input type="text" id="region" placeholder="Europe/Moscow" value="Europe/Moscow">
    </div>
  </div>
  <div class="card">
    <h2>Ночной режим</h2>
    <div class="field"><label><input type="checkbox" id="nightOn"> Включить</label></div>
    <div class="mini">
      <div class="field"><label>Начало (час)</label><input type="number" id="nightA" min="0" max="23" value="23"></div>
      <div class="field"><label>Конец (час)</label><input type="number" id="nightB" min="0" max="23" value="7"></div>
    </div>
  </div>
  <div class="card">
    <h2>Единицы</h2>
    <div class="field"><label>Температура</label>
      <select id="useF"><option value="0">Цельсий (°C)</option><option value="1">Фаренгейт (°F)</option></select>
    </div>
    <div class="field"><label>Время</label>
      <select id="use12"><option value="0">24-часовой</option><option value="1">12-часовой</option></select>
    </div>
  </div>
  <button class="btn btn-p" onclick="saveSettings()">💾 Сохранить и отправить в Mega</button>
  <div id="setStat" class="note"></div>
</div>

<!-- OTA -->
<div id="ota" class="content">
  <div class="card">
    <h2>Прошивка ESP8266</h2>
    <div class="row"><span class="lbl">Текущая версия</span><span class="val" id="ocur">—</span></div>
    <div class="row"><span class="lbl">Источник</span><span class="val">github.com/%REPO%</span></div>
    <div class="row"><span class="lbl">Проверка</span><span class="val">раз в час</span></div>
    <button class="btn btn-p" onclick="checkOta()">🔄 Проверить сейчас</button>
    <div id="otaStat" class="note"></div>
  </div>
  <div class="card">
    <h2>Как выпустить обновление</h2>
    <div class="note">
      1. В <code>config.h</code> поднимите <code>FIRMWARE_VERSION</code>.<br>
      2. Arduino IDE: <b>Sketch → Export Compiled Binary</b>.<br>
      3. На GitHub: <b>Releases → Draft a new release</b>, тег <code>v1.0.1</code>.<br>
      4. Прикрепите к релизу <code>.bin</code>-файл.<br>
      5. ESP увидит его при следующей проверке.
    </div>
  </div>
</div>

<!-- DEBUG -->
<div id="dbg" class="content">
  <div class="card">
    <h2>Логи</h2>
    <div class="log" id="log">Загрузка...</div>
    <button class="btn btn-p" onclick="refreshLog()">🔄 Обновить</button>
    <button class="btn btn-d" onclick="clearLog()" style="margin-left:6px">🗑 Очистить</button>
  </div>
</div>

<script>
function showTab(id){
  document.querySelectorAll('.content').forEach(e=>e.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(e=>e.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  document.querySelector('.tab[data-tab="'+id+'"]').classList.add('active');
  if(id==='dbg') refreshLog();
}
document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>showTab(t.dataset.tab));

async function jget(u){try{const r=await fetch(u);return await r.json();}catch(e){return null;}}

async function poll(){
  const j = await jget('/api/sensors');
  if(!j) return;
  document.getElementById('t').textContent = j.temp!=null ? j.temp.toFixed(1):'—';
  document.getElementById('h').textContent = j.hum !=null ? j.hum.toFixed(0) :'—';
  document.getElementById('p').textContent = j.press!=null? j.press.toFixed(0):'—';
  const m = document.getElementById('mega');
  if(j.online){m.className='status on';m.innerHTML='<span class="dot"></span>Online';}
  else{m.className='status off';m.innerHTML='<span class="dot"></span>Offline';}
  document.getElementById('vesp').textContent = j.version||'—';
  document.getElementById('age').textContent  = j.age!=null ? j.age+' с назад':'—';
  document.getElementById('rssi').textContent = j.rssi+' dBm';
  document.getElementById('up').textContent   = j.uptime;
  document.getElementById('ocur').textContent = j.version||'—';
}

async function saveSettings(){
  const d={
    region: document.getElementById('region').value,
    nightMode: document.getElementById('nightOn').checked,
    nightStart: +document.getElementById('nightA').value,
    nightEnd: +document.getElementById('nightB').value,
    useF: document.getElementById('useF').value==='1',
    use12h: document.getElementById('use12').value==='1'
  };
  const r = await fetch('/api/settings',{method:'POST',
    headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});
  const j = await r.json();
  document.getElementById('setStat').textContent = j.ok ? '✅ Сохранено' : ('❌ '+(j.error||'ошибка'));
  setTimeout(()=>document.getElementById('setStat').textContent='',4000);
}

async function checkOta(){
  document.getElementById('otaStat').textContent='Проверяем...';
  const r = await fetch('/api/ota/check',{method:'POST'});
  const j = await r.json();
  document.getElementById('otaStat').textContent = j.message||'?';
}

async function refreshLog(){
  const j = await jget('/api/debug/logs');
  const box = document.getElementById('log');
  if(!j){box.textContent='Ошибка';return;}
  box.textContent = (j.logs||[]).join('\n')||'(пусто)';
  box.scrollTop = box.scrollHeight;
}

async function clearLog(){
  await fetch('/api/debug/clear',{method:'POST'});
  document.getElementById('log').textContent='(очищено)';
}

setInterval(poll,3000); poll();
</script>
</body>
</html>
)HTML";

String webUiGetPage() {
    String html = FPSTR(INDEX_HTML);
    html.replace("%DEVICE_NAME%", DEVICE_NAME);
    html.replace("%DEVICE_LOCATION%", DEVICE_LOCATION);
    html.replace("%REPO%", GITHUB_REPO);
    return html;
}

String webUiGetSensorsJson() {
    JsonDocument d;
    if (!isnan(megaGetTemp()))  d["temp"]  = megaGetTemp();
    if (!isnan(megaGetHum()))   d["hum"]   = megaGetHum();
    if (!isnan(megaGetPress())) d["press"] = megaGetPress();
    d["online"]  = megaIsOnline();
    d["age"]     = megaIsOnline() ? (long)megaLastUpdateAge() : (long)-1;
    d["version"] = githubOtaCurrentVersion();
    d["rssi"]    = WiFi.RSSI();
    unsigned long s = millis() / 1000;
    char buf[16];
    sprintf(buf, "%02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
    d["uptime"] = String(buf);
    String out;
    serializeJson(d, out);
    return out;
}

String webUiGetOtaStatusJson() {
    JsonDocument d;
    d["current"]  = githubOtaCurrentVersion();
    d["repo"]     = GITHUB_REPO;
    d["interval"] = "1h";
    String out;
    serializeJson(d, out);
    return out;
}
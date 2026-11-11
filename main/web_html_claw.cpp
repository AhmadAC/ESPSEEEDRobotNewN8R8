// main\web_html_claw.cpp
#include "web_html.h"

const char HTML_CLAW_BASIC[] = R"raw_html(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Claw Fallback</title>
<style>
  body{background:#020617;color:#fff;font-family:sans-serif;padding:20px;max-width:500px;margin:auto;}
  .card{background:#0f172a;padding:20px;border-radius:10px;margin-bottom:20px;border:1px solid #1e293b;}
  button,select,input{width:100%;padding:12px;margin:5px 0;border-radius:8px;border:none;background:#1e293b;color:#fff;}
  button{background:#2563eb;font-weight:bold;cursor:pointer;margin-top:10px;}
  h2{color:#f59e0b;}
</style>
</head><body>
<h2>ESP32 Claw Diagnostics</h2>
<div class="card">
  <p style="color:#94a3b8; font-size:14px;"><b>Note:</b> Please use the iOS BLE App for full robot control. This UI is a low-power fallback for diagnostics and Wi-Fi setup.</p>
  <button style="background:#10b981;" onclick="fetch('/claw?cmd=open')">Open Claw</button>
  <button style="background:#ef4444;" onclick="fetch('/claw?cmd=close')">Close Claw</button>
</div>
<div class="card">
  <h3 style="margin-top:0;">Wi-Fi Setup</h3>
  <button onclick="fetch('/scan').then(r=>r.json()).then(d=>{let s=document.getElementById('ssid');s.innerHTML='';d.forEach(n=>s.innerHTML+='<option>'+n+'</option>');})">Scan Networks</button>
  <select id="ssid"><option>Scan first...</option></select>
  <input type="password" id="pass" placeholder="Wi-Fi Password">
  <button style="background:#10b981;" onclick="fetch('/save',{method:'POST',body:JSON.stringify({ssid:document.getElementById('ssid').value,pass:document.getElementById('pass').value})}).then(()=>alert('Rebooting...'))">Save & Connect</button>
</div>
</body></html>
)raw_html";
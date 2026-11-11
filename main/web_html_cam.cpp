// main\web_html_cam.cpp
#include "web_html.h"

const char HTML_CAM_BASIC[] = R"raw_html(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Camera Fallback</title>
<style>
  body{background:#020617;color:#fff;font-family:sans-serif;padding:20px;max-width:500px;margin:auto;}
  .card{background:#0f172a;padding:20px;border-radius:10px;margin-bottom:20px;border:1px solid #1e293b;}
  button,select,input{width:100%;padding:12px;margin:5px 0;border-radius:8px;border:none;background:#1e293b;color:#fff;}
  button{background:#2563eb;font-weight:bold;cursor:pointer;margin-top:10px;}
  h2{color:#10b981;}
</style>
</head><body>
<h2>ESP32 Camera Diagnostics</h2>
<div class="card">
  <p style="color:#94a3b8; font-size:14px;"><b>Note:</b> Please use the Python Controller App for streaming. This UI is a low-power fallback for Wi-Fi setup.</p>
  <button onclick="window.open('http://' + window.location.hostname + ':81/', '_blank')">View Raw TCP Stream</button>
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
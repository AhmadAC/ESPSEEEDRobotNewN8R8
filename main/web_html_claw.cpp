// main/web_html_claw.cpp
#include "web_html.h"

// -------------------------------------------------------------
// DYNAMIC CLAW HTML PAYLOADS
// -------------------------------------------------------------
const char HTML_CLAW_UI[] = R"raw_html(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 Claw Controller</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
        :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
        body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; margin: 0; }
        .container { max-width: 600px; margin: 0 auto; }
        h1 { text-align: center; color: var(--primary); margin-bottom: 25px; }
        .card { background: var(--card); border-radius: 16px; padding: 25px; border: 1px solid #334155; margin-bottom: 25px; box-sizing: border-box; }
        h2 { border-bottom: 1px solid #334155; padding-bottom: 10px; color: var(--primary); margin-top: 0; margin-bottom: 15px; }
        .grid { display: grid; grid-template-columns: 1fr; gap: 15px; }
        @media (min-width: 500px) { .grid { grid-template-columns: 1fr 1fr; } }
        button { background: #3b82f6; color: white; border: none; padding: 14px 15px; border-radius: 10px; cursor: pointer; font-size: 15px; width: 100%; font-weight: bold; transition: opacity 0.2s, transform 0.1s; }
        button:hover { opacity: 0.9; }
        button:active { transform: scale(0.96); }
        .btn-green { background: #10b981; }
        .btn-red { background: #ef4444; }
        .btn-orange { background: #f59e0b; }
        .btn-gray { background: #475569; }
        input[type=range] { width: 100%; margin: 15px 0; accent-color: var(--primary); }
        .status-box { background: #0f172a; border: 1px solid #334155; border-radius: 10px; padding: 12px; margin-bottom: 15px; text-align: center; font-weight: bold; }
        .angle-val { font-size: 1.5rem; color: var(--primary); }
        .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-align: center; font-size: 0.85rem; border: 1px solid; text-transform: uppercase; background: #172554; color: #93c5fd; border-color: #3b82f6; margin-bottom: 20px; }
    </style>
</head>
<body>
<div class='container'>
    <div class="status-bar">ESP32-S3 Robotic Claw Controller</div>
    <div class='card'>
        <h2>Claw Gripper Control</h2>
        <div class='status-box'>
            <div>Current Angle: <span class='angle-val' id='val_angle'>90&deg;</span></div>
            <div style="font-size:0.85rem; color:#94a3b8; margin-top:5px;">Last Command: <span id='last_cmd' style="color:#f1f5f9;">--</span></div>
        </div>
        <input type='range' id='claw_slider' min='0' max='138' value='90' oninput='updateSlider(this.value)'>
        <div class='grid' style='margin-bottom:15px;'>
            <button class='btn-green' onclick='sendCmd("open")'>Fully Open (138&deg;)</button>
            <button class='btn-red' onclick='sendCmd("close")'>Fully Close (0&deg;)</button>
            <button class='btn-orange' onclick='sendCmd("half_open")'>Half Open (100&deg;)</button>
            <button class='btn-orange' onclick='sendCmd("half_close")'>Half Close (45&deg;)</button>
        </div>
        <button class='btn-gray' onclick='sendCmd("stop")'>Relax Servo (0 Amps)</button>
    </div>
    <div class='card'>
        <h2>Hardware Profile Switch</h2>
        <div class='grid'>
            <button class='btn-gray' onclick='setMode("robot")'>Robot Mode</button>
            <button class='btn-gray' onclick='setMode("cam")'>Camera Mode</button>
        </div>
    </div>
</div>
<script>
    function sendCmd(cmd) {
        fetch('/claw?cmd=' + encodeURIComponent(cmd))
            .then(r => r.text())
            .then(() => fetchStatus());
    }
    function updateSlider(val) {
        document.getElementById('val_angle').innerHTML = val + '&deg;';
        fetch('/claw?angle=' + encodeURIComponent(val));
    }
    function fetchStatus() {
        fetch('/status')
            .then(r => r.json())
            .then(d => {
                document.getElementById('last_cmd').innerText = d.last_cmd;
                document.getElementById('val_angle').innerHTML = d.angle + '&deg;';
                document.getElementById('claw_slider').value = d.angle;
            }).catch(() => {});
    }
    function setMode(m) {
        if(confirm("Switch to " + m.toUpperCase() + " profile and reboot?")) {
            fetch('/switch_mode', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ mode: m })
            }).then(() => alert('Rebooting...'));
        }
    }
    setInterval(fetchStatus, 1500);
    window.onload = fetchStatus;
</script>
</body>
</html>
)raw_html";

const char HTML_CLAW_BASIC[] = R"raw_html(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Claw Diagnostics</title>
<style>
  body{background:#020617;color:#fff;font-family:sans-serif;padding:20px;max-width:500px;margin:auto;}
  .card{background:#0f172a;padding:20px;border-radius:10px;margin-bottom:20px;border:1px solid #1e293b;}
  button{width:100%;padding:12px;margin:5px 0;border-radius:8px;border:none;background:#2563eb;color:#fff;font-weight:bold;cursor:pointer;}
  h2{color:#38bdf8;}
</style>
</head><body>
<h2>ESP32 Claw Diagnostics</h2>
<div class="card">
  <button onclick="fetch('/claw?cmd=open')">Open Claw</button>
  <button onclick="fetch('/claw?cmd=close')">Close Claw</button>
  <button onclick="fetch('/claw?cmd=stop')" style="background:#ef4444;">Relax Claw</button>
</div>
</body></html>
)raw_html";
// main\web_html_cam.cpp
#include "web_html.h"

// -------------------------------------------------------------
// DYNAMIC CAM HTML PAYLOADS
// -------------------------------------------------------------
const char HTML_CAM_SETUP[] = R"raw_html(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>ESP Setup</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
    body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; display: flex; flex-direction: column; align-items: center; min-height: 100vh; margin:0;}
    .container { width: 100%; max-width: 420px; }
    .card { background: var(--card); padding: 25px; border-radius: 20px; border: 1px solid #334155; text-align: center; margin-top: 15px; }
    h2 { color: var(--primary); margin-top: 0; }
    input, select { width: 100%; padding: 12px; margin: 8px 0 20px; border-radius: 10px; border: 1px solid #475569; background: #0f172a; color: white; box-sizing: border-box; }
    button { width: 100%; padding: 15px; border: none; border-radius: 12px; font-weight: bold; cursor: pointer; color: white; margin-top:10px; transition: transform 0.1s; }
    button:active { transform: scale(0.96); opacity: 0.9; }
    .btn-green { background: #10b981; }
    .btn-red { background: #ef4444; }
    .btn-blue { background: #3b82f6; }
    .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-align: center; font-size: 0.85rem; border: 1px solid; text-transform: uppercase; background: #451a03; color: #fbbf24; border-color: #f59e0b; }
</style></head>
<body>
    <div class="container">
        <div class="status-bar">WIFI SETUP MODE</div>
        <div class="card">
            <h2>WiFi Setup</h2>
            <div id="status-msg" style="font-size:0.8rem;color:#64748b;margin-bottom:5px">Ready to Scan</div>
            <button class="btn-green" onclick="scan()">Scan Networks</button>
            <select id="ssid" style="margin-top:10px;"><option value="">-- Select --</option></select>
            <input type="password" id="pass" placeholder="Password">
            <button class="btn-green" onclick="save()">Save and Reboot</button>
            <button class="btn-blue" style="margin-top:15px;" onclick="location.href='/app'">Skip to Live Stream</button>
            
            <hr style="border-color:#334155; margin: 25px 0;">
            <div style="font-size:0.85rem; color:#94a3b8; margin-bottom:10px;">Quick Boot Mode Switch</div>
            <button style="background:#f59e0b;" onclick="forceAP()">Force AP Mode</button>
            <button style="background:#10b981;" onclick="forceWiFi()">Use Saved Wi-Fi</button>
            
            <button class="btn-red" style="margin-top:25px;" onclick="resetData()">Factory Reset Device</button>
        </div>
    </div>
    <script>
    function scan(){
        document.getElementById('status-msg').innerText="Scanning...";
        fetch('/scan').then(r=>{
            if(!r.ok) throw new Error("Server returned error");
            return r.json();
        }).then(d=>{
            const s=document.getElementById('ssid'); s.innerHTML='<option value="">-- Select --</option>';
            d.forEach(n=>{let o=document.createElement('option');o.value=n;o.innerText=n;s.appendChild(o)});
            document.getElementById('status-msg').innerText="Networks Found: " + d.length;
        }).catch(()=>{ document.getElementById('status-msg').innerText="Scan Error"; });
    }
    function save(){
        const s=document.getElementById('ssid').value, p=document.getElementById('pass').value;
        if(!s) return alert('Select SSID');
        fetch('/save', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ssid: s, pass: p}) })
        .then(r=>r.text()).then(t=>{ alert('Saved! Rebooting...'); });
    }
    function forceAP(){
        if(confirm("Switch to AP Mode and reboot?")) fetch('/switch_to_ap', { method: 'POST' }).then(() => alert('Rebooting...'));
    }
    function forceWiFi(){
        if(confirm("Switch to Wi-Fi Mode and reboot?")) fetch('/switch_to_wifi', { method: 'POST' }).then(() => alert('Rebooting...'));
    }
    function resetData(){
        if(confirm("Are you sure?")) fetch('/reset_cal', { method: 'POST' }).then(() => alert('Resetting...'));
    }
    </script>
</body></html>
)raw_html";

const char HTML_CAM_APP[] = R"raw_html(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Camera Live Stream</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
    body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; display: flex; flex-direction: column; align-items: center; min-height: 100vh; margin:0;}
    .container { width: 100%; max-width: 600px; }
    .card { background: var(--card); padding: 25px; border-radius: 20px; border: 1px solid #334155; text-align: center; margin-top: 15px; }
    h1 { color: #00adb5; margin-bottom: 20px; }
    .stream-container { width: 100%; border-radius: 8px; overflow: hidden; background-color: #000; margin-bottom: 20px; position: relative; min-height: 200px; display: flex; align-items: center; justify-content: center; }
    img { display: block; width: 100%; height: auto; transition: transform 0.2s; }
    button { width: 100%; padding: 15px; border: none; border-radius: 12px; font-weight: bold; cursor: pointer; color: white; transition: transform 0.1s; }
    button:active { transform: scale(0.96); opacity: 0.9; }
    button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
    .btn-blue { background: #3b82f6; margin-top:10px; }
    .btn-gray { background: #475569; }
    .btn-purple { background: #8b5cf6; }
    .btn-orange { background: #f59e0b; }
    .btn-green { background: #10b981; }
    .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-align: center; font-size: 0.85rem; border: 1px solid; text-transform: uppercase; background: #172554; color: #93c5fd; border-color: #3b82f6; }
    .status-text { margin-top: 10px; color: #aaaaaa; font-size: 14px; }
    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 10px; }
</style>
</head>
<body>
    <div class="container">
        <div class="status-bar">CAMERA LIVE STREAM</div>
        <div class="card">
            <div class="stream-container">
                <img id="streamImg" alt="Live Stream">
            </div>
            
            <div class="grid-2">
                <button id="snapBtn" class="btn-green" onclick="takeSnapshot()">Save HD Picture</button>
                <button class="btn-orange" onclick="rotateCamera()">Rotate 90&deg;</button>
            </div>
            <div id="status" class="status-text">Streaming live...</div>
            
            <button class="btn-gray" style="margin-top:25px; background:#0f172a; border:1px solid #475569;" onclick="location.href='/setup'">Go to Wi-Fi Setup</button>
            <div class="grid-2">
                <button style="background:#f59e0b;" onclick="forceAP()">Force AP</button>
                <button style="background:#10b981;" onclick="forceWiFi()">Use Wi-Fi</button>
            </div>

            <hr style="border-color:#334155; margin: 25px 0;">
            <div style="font-size:0.85rem; color:#94a3b8; margin-bottom:10px;">Hardware Profile (Reboot Required)</div>
            <div class="grid-2">
                <button class="btn-gray" onclick="setMode('claw')">Claw Mode</button>
                <button class="btn-gray" onclick="setMode('robot')">Robot Mode</button>
            </div>
        </div>
    </div>

    <script>
        document.getElementById('streamImg').src = 'http://' + window.location.hostname + ':81/';

        let camRotation = 0;
        function rotateCamera() {
            camRotation = (camRotation + 90) % 360;
            const img = document.getElementById('streamImg');
            if (camRotation % 180 !== 0) {
                img.style.transform = `rotate(${camRotation}deg) scale(0.75)`;
            } else {
                img.style.transform = `rotate(${camRotation}deg) scale(1)`;
            }
        }

        function takeSnapshot() {
            const btn = document.getElementById('snapBtn');
            const status = document.getElementById('status');
            btn.disabled = true;
            status.innerText = "Capturing high-resolution image...";

            fetch('/capture')
                .then(response => {
                    if (!response.ok) throw new Error('Capture failed');
                    return response.blob();
                })
                .then(blob => {
                    if (camRotation === 0) {
                        downloadBlob(blob);
                    } else {
                        const img = new Image();
                        img.onload = () => {
                            const canvas = document.createElement('canvas');
                            const ctx = canvas.getContext('2d');
                            if (camRotation % 180 !== 0) {
                                canvas.width = img.naturalHeight;
                                canvas.height = img.naturalWidth;
                            } else {
                                canvas.width = img.naturalWidth;
                                canvas.height = img.naturalHeight;
                            }
                            ctx.translate(canvas.width/2, canvas.height/2);
                            ctx.rotate(camRotation * Math.PI / 180);
                            ctx.drawImage(img, -img.naturalWidth/2, -img.naturalHeight/2);
                            canvas.toBlob(rotatedBlob => {
                                downloadBlob(rotatedBlob);
                            }, 'image/jpeg', 0.95);
                        };
                        img.src = URL.createObjectURL(blob);
                    }
                })
                .catch(err => {
                    console.error(err);
                    status.innerText = "Error taking snapshot.";
                    alert("Capture Error! Check Serial Monitor.");
                    btn.disabled = false;
                });
        }
        
        function downloadBlob(blob) {
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.style.display = 'none';
            a.href = url;
            a.download = `snapshot_${new Date().toISOString().replace(/[:.]/g, '-')}.jpg`;
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
            const status = document.getElementById('status');
            status.innerText = "Snapshot saved!";
            document.getElementById('snapBtn').disabled = false;
            setTimeout(() => { status.innerText = "Streaming live..."; }, 3000);
        }

        function forceAP(){ if(confirm("Switch to AP Mode and reboot?")) fetch('/switch_to_ap', { method: 'POST' }).then(() => alert('Rebooting...')); }
        function forceWiFi(){ if(confirm("Switch to Wi-Fi Mode and reboot?")) fetch('/switch_to_wifi', { method: 'POST' }).then(() => alert('Rebooting...')); }
        function setMode(m){
            if(confirm("Switch to " + m.toUpperCase() + " profile and reboot?")) {
                fetch('/switch_mode', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({mode:m}) }).then(() => alert('Rebooting...'));
            }
        }
    </script>
</body></html>
)raw_html";
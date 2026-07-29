// main\web_html_claw.cpp
#include "web_html.h"

// -------------------------------------------------------------
// DYNAMIC CLAW HTML PAYLOAD
// -------------------------------------------------------------
const char HTML_CLAW_UI[] = R"raw_html(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Claw Controller</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
    body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; display: flex; flex-direction: column; align-items: center; margin:0; }
    .container { width: 100%; max-width: 500px; }
    .card { background: var(--card); padding: 25px; border-radius: 20px; border: 1px solid #334155; text-align: center; margin-bottom: 20px; }
    h2 { color: var(--primary); margin-top: 0; }
    input[type=password], select { width: 100%; padding: 12px; margin: 8px 0 20px; border-radius: 10px; border: 1px solid #475569; background: #0f172a; color: white; box-sizing: border-box; }
    input[type=range] { width: 100%; margin: 15px 0; accent-color: #0ea5e9; }
    button { width: 100%; padding: 15px; border: none; border-radius: 12px; font-weight: bold; cursor: pointer; color: white; margin-top:10px; transition: transform 0.1s; }
    button:active { transform: scale(0.96); opacity: 0.9; }
    button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
    .btn-green { background: #10b981; }
    .btn-red { background: #ef4444; }
    .btn-blue { background: #3b82f6; }
    .btn-purple { background: #8b5cf6; }
    .btn-gray { background: #475569; }
    .btn-orange { background: #f59e0b; }
    .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-align: center; font-size: 0.85rem; border: 1px solid; text-transform: uppercase; background: #172554; color: #93c5fd; border-color: #3b82f6; margin-bottom:15px; }
    .text-sm { font-size: 0.85rem; color: #94a3b8; margin-bottom:10px; }
    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 10px; }
</style>
</head>
<body>
    <div class="container">
        <div class="status-bar">CLAW DASHBOARD</div>

        <div class="card">
            <h2>Control Claw</h2>
            <div class="text-sm"><b>State:</b> <span id="lastcmd" style="color:#3b82f6;">Loading...</span> | <b>Angle:</b> <span id="curangle" style="color:#10b981;">--</span>°</div>
            
            <input type="range" id="angleSlider" min="0" max="180" value="90" oninput="updateAngleLabel(this.value)" onchange="setAngle(this.value)">
            <div class="text-sm" style="margin-bottom: 20px;">Target Slider: <span id="sliderVal">90</span>°</div>

            <div class="grid-2">
                <button class="btn-green" onclick="c('open')">Open (180°)</button>
                <button class="btn-red" onclick="c('close')">Close (0°)</button>
                <button class="btn-blue" onclick="c('half_open')">Half Open</button>
                <button class="btn-purple" onclick="c('half_close')">Half Close</button>
            </div>
            
            <hr style="border-color:#334155; margin: 15px 0;">
            <div class="text-sm">PyController Gamepad Setup</div>
            <button class="btn-purple" onclick="pairESPNOW()">Connect to PyController</button>
            
            <p id="cstat" class="text-sm" style="margin-top:15px;"></p>
        </div>
        
        <div class="card" id="camCard">
            <h2>Live Camera</h2>
            <div class="stream-container" id="streamContainer" style="display:none; width: 100%; border-radius: 8px; overflow: hidden; background-color: #000; margin-bottom: 15px; min-height: 200px; position: relative; flex-direction: column; align-items: center; justify-content: center;">
                <img id="streamImg" alt="Live Stream" style="display: block; width: 100%; height: auto; transition: transform 0.2s;">
            </div>
            <div id="camControls" style="display:none; margin-bottom: 15px;">
                <div class="grid-2">
                    <button id="snapBtn" class="btn-green" onclick="takeSnapshot()">Save HD Picture</button>
                    <button class="btn-orange" onclick="rotateCamera()">Rotate 90&deg;</button>
                </div>
                <div id="camStatus" class="text-sm" style="margin-top: 10px;">Streaming live...</div>
            </div>
            <button id="camToggleBtn" class="btn-blue" onclick="toggleCamera()">Turn Camera ON</button>
        </div>

        <div class="card">
            <h2>Wi-Fi Settings</h2>
            <div id="status" class="text-sm">Ready to Scan</div>
            <button class="btn-gray" onclick="scan()">Scan Networks</button>
            
            <select id="ssid" style="margin-top:15px;"><option value="">-- Select --</option></select>
            <input type="password" id="pass" placeholder="Password">
            
            <button class="btn-green" onclick="save()">Save and Connect</button>
            
            <hr style="border-color:#334155; margin: 25px 0;">
            <div class="text-sm">Quick Boot Mode Switch</div>
            <div class="grid-2">
                <button style="background:#f59e0b;" onclick="forceAP()">Force AP</button>
                <button style="background:#10b981;" onclick="forceWiFi()">Use Wi-Fi</button>
            </div>
            
            <hr style="border-color:#334155; margin: 25px 0;">
            <div class="text-sm">Hardware Profile (Reboot Required)</div>
            <div class="grid-2">
                <button class="btn-purple" disabled>✓ Claw Mode</button>
                <button class="btn-gray" onclick="setMode('robot')">Robot Mode</button>
                <button class="btn-gray" onclick="setMode('cam')">Camera Mode</button>
            </div>
        </div>
    </div>

    <script>
        function c(cmd){
            document.getElementById('cstat').innerText = 'Sending Preset...';
            fetch('/claw?cmd='+cmd).then(r=>r.text()).then(t=>{
                document.getElementById('cstat').innerText = 'Status: ' + t;
                setTimeout(loadStatus, 600);
            });
        }
        function setAngle(val){
            document.getElementById('cstat').innerText = 'Setting Angle...';
            fetch('/claw?angle='+val).then(r=>r.text()).then(t=>{
                document.getElementById('cstat').innerText = 'Status: ' + t;
                setTimeout(loadStatus, 600);
            });
        }
        function updateAngleLabel(val){ document.getElementById('sliderVal').innerText = val; }
        
        function pairESPNOW() {
            document.getElementById('cstat').innerText = 'Pairing with PyController...';
            fetch('/espnow_pair', {method: 'POST'}).then(r=>r.text()).then(t=>{
                document.getElementById('cstat').innerText = 'Status: ' + t;
                setTimeout(loadStatus, 600);
            });
        }

        let camActive = false;
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
        
        function toggleCamera() {
            camActive = !camActive;
            const container = document.getElementById('streamContainer');
            const img = document.getElementById('streamImg');
            const btn = document.getElementById('camToggleBtn');
            const ctrl = document.getElementById('camControls');
            if (camActive) {
                img.src = 'http://' + window.location.hostname + ':81/';
                container.style.display = 'flex';
                ctrl.style.display = 'block';
                btn.innerText = 'Turn Camera OFF';
                btn.className = 'btn-red';
            } else {
                img.src = '';
                container.style.display = 'none';
                ctrl.style.display = 'none';
                btn.innerText = 'Turn Camera ON';
                btn.className = 'btn-blue';
            }
        }
        
        function takeSnapshot() {
            const btn = document.getElementById('snapBtn');
            const status = document.getElementById('camStatus');
            btn.disabled = true;
            status.innerText = "Capturing high-resolution image...";

            fetch('/capture')
                .then(r => {
                    if(!r.ok) throw new Error('Capture failed');
                    return r.blob();
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
            const status = document.getElementById('camStatus');
            status.innerText = "Snapshot saved!";
            document.getElementById('snapBtn').disabled = false;
            setTimeout(() => { status.innerText = "Streaming live..."; }, 3000);
        }
        
        function scan(){
            document.getElementById('status').innerText = 'Scanning...';
            fetch('/scan').then(r=>r.json()).then(d=>{
                let s = document.getElementById('ssid');
                s.innerHTML = '<option value="">-- Select --</option>';
                d.forEach(n=>{ s.innerHTML += '<option value="'+n+'">'+n+'</option>'; });
                document.getElementById('status').innerText = 'Found ' + d.length + ' networks';
            }).catch(() => document.getElementById('status').innerText = 'Scan Error');
        }
        function save(){
            let s = document.getElementById('ssid').value, p = document.getElementById('pass').value;
            if(!s) return alert('Select SSID');
            fetch('/save', { method:'POST', body:JSON.stringify({ssid:s, pass:p}) }).then(()=>{
                alert('Credentials saved! Rebooting...');
            });
        }
        function forceAP(){ if(confirm("Switch to AP Mode and reboot?")) fetch('/switch_to_ap', { method: 'POST' }).then(() => alert('Rebooting...')); }
        function forceWiFi(){ if(confirm("Switch to Wi-Fi Mode and reboot?")) fetch('/switch_to_wifi', { method: 'POST' }).then(() => alert('Rebooting...')); }
        function setMode(m){
            if(confirm("Switch to " + m.toUpperCase() + " profile and reboot?")) {
                fetch('/switch_mode', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({mode:m}) }).then(() => alert('Rebooting...'));
            }
        }

        function loadStatus(){
            fetch('/status').then(r=>r.json()).then(d=>{
                document.getElementById('lastcmd').innerText = d.last_cmd;
                document.getElementById('curangle').innerText = d.angle;
                if (document.activeElement !== document.getElementById('angleSlider')) {
                    document.getElementById('angleSlider').value = d.angle;
                    document.getElementById('sliderVal').innerText = d.angle;
                }
            });
        }
        setInterval(loadStatus, 3000);
        window.onload = loadStatus;
    </script>
</body></html>
)raw_html";
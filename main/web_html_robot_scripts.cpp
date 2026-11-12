// main/web_html_robot_scripts.cpp
#include "web_html.h"

// Gamepad HTML shell + Client JavaScript routines
const char html_part4[] = R"raw_html(
            </div>
        </div>
    </div>

    <!-- GAMEPAD CARD -->
    <div class='card' id='gamepad_card'>
        <h2>Gamepad Controller Mapping</h2>
        <p class="text-sm" style="margin-top:0px; margin-bottom: 20px;">Use your Switch Pro Controller or generic gamepad over WebSocket</p>
        <div id="gamepads-container">
            <div class="no-gamepad">
                <h2><span class="pulse"></span> Waiting for Gamepad...</h2>
                <p>Connect your Switch Pro Controller or Gamepad via Bluetooth/USB and <strong>press any button</strong> to wake it up.</p>
            </div>
        </div>
    </div>

</div>

<script>
    let localSensorEnabled = false; 
    let syncEnabled = false;
    let servoTimeouts = {}; 
    let allCalibrations = {};
    let activeDrag = null;
    const legMap = { 'low_left': 'll', 'high_right': 'hr', 'high_left': 'hl', 'low_right': 'lr' };

    let camActive = false;
    let camRotation = 0;
    
    // --- WebSocket & Gamepad Logic ---
    let ws;
    function initWebSocket() {
        ws = new WebSocket('ws://' + window.location.host + '/ws');
        ws.onopen = () => console.log('WebSocket Connected for Gamepad');
        ws.onclose = () => setTimeout(initWebSocket, 2000);
    }
    initWebSocket();

    const switchProButtonMap = {
        0: "B (Stop)",
        1: "A (Stand)",
        2: "Y (Sit)",
        3: "X (Leap)",
        4: "L (Stretch Down)",
        5: "R (Stretch Back)",
        6: "ZL (Crawl)",
        7: "ZR",
        8: "-",
        9: "+",
        10: "LJS Press",
        11: "RJS Press",
        12: "Up (Fwd)",
        13: "Down (Back)",
        14: "Left (Wave)",
        15: "Right (Wave)",
        16: "Home"
    };

    const gamepadContainer = document.getElementById('gamepads-container');
    let lastBtnState = {};
    let lastAxisState = {};

    function handleGamepadInput(bIndex, gp) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return;
        let action = "";
        
        if (bIndex === 12) action = "forward";      
        else if (bIndex === 13) action = "backward";
        else if (bIndex === 14) action = "left_wave";
        else if (bIndex === 15) action = "right_wave";
        else if (bIndex === 0) action = "stop";      
        else if (bIndex === 1) action = "stand";     
        else if (bIndex === 2) action = "sit";       
        else if (bIndex === 3) action = "leap_forward"; 
        else if (bIndex === 4) action = "stretch_down"; 
        else if (bIndex === 5) action = "stretch_back"; 
        else if (bIndex === 6) action = "crawl";
        
        if (action !== "") {
            ws.send(JSON.stringify({action: action}));
            if (allCalibrations[action]) {
                let p = allCalibrations[action];
                for (let leg in legMap) {
                    let val = p[legMap[leg]];
                    let el = document.getElementById(leg);
                    if (el) el.value = val;
                    let valEl = document.getElementById('val_' + leg);
                    if (valEl) valEl.innerHTML = val + '&deg;';
                }
            }
        }
    }

    function updateGamepadStatus() {
        const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
        let activeGamepadFound = false;
        let htmlContent = '';

        for (let i = 0; i < gamepads.length; i++) {
            const gp = gamepads[i];
            if (gp) {
                activeGamepadFound = true;
                const idLower = gp.id.toLowerCase();
                const isSwitchPro = idLower.includes('057e') || idLower.includes('switch') || idLower.includes('pro') || gp.mapping === 'standard';
                
                const badgeHtml = isSwitchPro 
                    ? `<div class="detected-badge">✓ Nintendo Switch Controller Mapped</div>` 
                    : `<div class="detected-badge" style="border-color:#777; color:#aaa;">Generic Gamepad</div>`;

                htmlContent += `
                    <div class="gp-header">
                        <div class="gp-title">${gp.id}</div>
                        <div class="gp-meta">Index: ${gp.index} | Mapping: ${gp.mapping || 'unknown'}</div>
                        ${badgeHtml}
                    </div>
                    <div class="section-title">Buttons</div>
                    <div class="buttons-grid">
                `;

                gp.buttons.forEach((button, bIndex) => {
                    const isPressed = button.pressed;
                    
                    if (isPressed && !lastBtnState[bIndex]) {
                        handleGamepadInput(bIndex, gp);
                    }
                    lastBtnState[bIndex] = isPressed;

                    const val = button.value.toFixed(2);
                    const pressedClass = isPressed ? 'pressed' : '';
                    const labelName = switchProButtonMap[bIndex] || `B${bIndex}`;

                    htmlContent += `
                        <div class="btn-card ${pressedClass}">
                            <div class="btn-label">${labelName}</div>
                            <div class="btn-id">(B${bIndex})</div>
                            <div class="btn-val">${val}</div>
                        </div>
                    `;
                });

                htmlContent += `</div><div class="section-title">Joysticks</div><div class="axes-container">`;

                gp.axes.forEach((axisVal, aIndex) => {
                    if (aIndex === 1 || aIndex === 3) {
                        if (axisVal < -0.5 && lastAxisState[aIndex] !== 'up') {
                            lastAxisState[aIndex] = 'up';
                            if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({action: "forward"}));
                        } else if (axisVal > 0.5 && lastAxisState[aIndex] !== 'down') {
                            lastAxisState[aIndex] = 'down';
                            if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({action: "backward"}));
                        } else if (Math.abs(axisVal) <= 0.2 && lastAxisState[aIndex] !== 'center') {
                            lastAxisState[aIndex] = 'center';
                            if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({action: "stop"}));
                        }
                    }

                    const val = axisVal.toFixed(2);
                    let fillLeft = '50%';
                    let fillWidth = '0%';
                    if (axisVal > 0) { fillLeft = '50%'; fillWidth = (axisVal * 50) + '%'; }
                    else if (axisVal < 0) { fillLeft = (50 + (axisVal * 50)) + '%'; fillWidth = (Math.abs(axisVal) * 50) + '%'; }

                    htmlContent += `
                        <div class="axis-row">
                            <div class="axis-label">Axis ${aIndex}</div>
                            <div class="axis-bar-bg"><div class="axis-bar-fill" style="left: ${fillLeft}; width: ${fillWidth};"></div></div>
                            <div class="axis-value">${val}</div>
                        </div>
                    `;
                });

                htmlContent += `</div>`;
            }
        }

        if (activeGamepadFound) {
            gamepadContainer.innerHTML = htmlContent;
        } else {
            gamepadContainer.innerHTML = `
                <div class="no-gamepad">
                    <h2><span class="pulse"></span> Waiting for Gamepad...</h2>
                    <p>Connect your Switch Pro Controller or Gamepad via Bluetooth/USB and <strong>press any button</strong> to wake it up.</p>
                </div>
            `;
        }

        requestAnimationFrame(updateGamepadStatus);
    }
    requestAnimationFrame(updateGamepadStatus);

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

    function fetchJSON(url, bodyData) {
        return fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(bodyData)
        });
    }

    function forceAP(){
        if(confirm("Switch to AP Mode and reboot?")) fetch('/switch_to_ap', { method: 'POST' }).then(() => alert('Rebooting...'));
    }
    function forceWiFi(){
        if(confirm("Switch to Wi-Fi Mode and reboot?")) fetch('/switch_to_wifi', { method: 'POST' }).then(() => alert('Rebooting...'));
    }
    function setMode(m){
        if(confirm("Switch to " + m.toUpperCase() + " profile and reboot?")) {
            fetchJSON('/switch_mode', {mode: m}).then(() => alert('Rebooting...'));
        }
    }

    function dragStart(id) { activeDrag = id; }
    function dragEnd() { activeDrag = null; }

    function updateVolume(val) {
        let el = document.getElementById('val_volume');
        if (el) el.innerText = val + "%";
        fetchJSON('/audio_config', { volume: parseInt(val) });
    }

    function testAudio() {
        fetchJSON('/audio_play', { sound: 'dog_bark' });
    }
    
    function playBeep() {
        fetch('/beep');
    }

    function applyPins(b, l, d) {
        fetch(`/set_pins?bclk=${b}&lrc=${l}&din=${d}`)
            .then(r => r.text())
            .then(t => {
                updatePinButtons(b, l);
                setTimeout(playBeep, 150);
            });
    }

    function updatePinButtons(bclk, lrc) {
        let y = document.getElementById("btnPinYD");
        let s = document.getElementById("btnPinSeeed");
        let w = document.getElementById("btnPinSwap");
        if (y) y.classList.remove("active");
        if (s) s.classList.remove("active");
        if (w) w.classList.remove("active");

        if (bclk === 8 && s) {
            s.classList.add("active");
        } else if (bclk === 12 && w) {
            w.classList.add("active");
        } else if (y) {
            y.classList.add("active");
        }
    }
    
    function stopAudio() {
        fetch('/audio_stop', { method: 'POST' });
    }
    
    function recordAudio(btn) {
        btn.disabled = true;
        btn.innerText = "Recording (3s)...";
        fetchJSON('/audio_play', { sound: "record_3s" });
        
        setTimeout(() => {
            btn.innerText = "Playing...";
        }, 3000);
        
        setTimeout(() => {
            btn.innerText = "Record & Play (3s)";
            btn.disabled = false;
        }, 6000);
    }

    let micStreaming = false;
    function toggleMicStream() {
        let audioEl = document.getElementById('mic_stream');
        let btn = document.getElementById('btn_mic_stream');
        micStreaming = !micStreaming;
        if(micStreaming) {
            audioEl.src = 'http://' + window.location.hostname + ':82/';
            audioEl.play().catch(e => {
                alert("Autoplay blocked. Please click play on the audio control.");
            });
            btn.innerText = 'Stop Live Mic';
            btn.classList.remove('btn-blue');
            btn.classList.add('btn-red');
        } else {
            audioEl.pause();
            audioEl.src = '';
            btn.innerText = 'Start Live Mic';
            btn.classList.remove('btn-red');
            btn.classList.add('btn-blue');
        }
    }

    function toggleSync() {
        syncEnabled = !syncEnabled;
        let btn = document.getElementById('btn_sync');
        if (syncEnabled) {
            btn.innerText = "Sync Legs: ON";
            btn.classList.remove('btn-gray'); btn.classList.add('btn-green');
        } else {
            btn.innerText = "Sync Legs: OFF";
            btn.classList.remove('btn-green'); btn.classList.add('btn-gray');
        }
    }

    function isDragging(leg) {
        if (leg === activeDrag) return true;
        if (syncEnabled && activeDrag) {
            if (activeDrag === 'high_left' && leg === 'high_right') return true;
            if (activeDrag === 'high_right' && leg === 'high_left') return true;
            if (activeDrag === 'low_left' && leg === 'low_right') return true;
            if (activeDrag === 'low_right' && leg === 'low_left') return true;
        }
        return false;
    }

    function fetchCalibrations() {
        fetch('/calibrations_json').then(r => r.json()).then(data => {
            allCalibrations = data;
            loadCalibTarget();
        });
    }

    function loadCalibTarget() {
        let tgt = document.getElementById('calib_target').value;
        if (allCalibrations[tgt]) {
            document.getElementById('cal_ll').value = allCalibrations[tgt].ll;
            document.getElementById('cal_hr').value = allCalibrations[tgt].hr;
            document.getElementById('cal_hl').value = allCalibrations[tgt].hl;
            document.getElementById('cal_lr').value = allCalibrations[tgt].lr;
        }
    }

    function testCalib() {
        let tgt = document.getElementById('calib_target').value;
        fetchJSON('/calibrate', {
            target: tgt,
            ll: parseInt(document.getElementById('cal_ll').value) || 0,
            hr: parseInt(document.getElementById('cal_hr').value) || 0,
            hl: parseInt(document.getElementById('cal_hl').value) || 0,
            lr: parseInt(document.getElementById('cal_lr').value) || 0,
            save: false
        });
    }

    function saveCalib() {
        let tgt = document.getElementById('calib_target').value;
        fetchJSON('/calibrate', {
            target: tgt,
            ll: parseInt(document.getElementById('cal_ll').value) || 0,
            hr: parseInt(document.getElementById('cal_hr').value) || 0,
            hl: parseInt(document.getElementById('cal_hl').value) || 0,
            lr: parseInt(document.getElementById('cal_lr').value) || 0,
            save: true
        }).then(()=> {
            alert('Calibration Updated and Saved to ESP32 Storage!');
            fetchCalibrations();
        });
    }

    function downloadCalib() { window.location.href = '/download_cal'; }

    function resetCal() {
        if (confirm("Are you sure you want to clear NVS storage? This will clear custom poses/offsets and reboot to reload firmware defaults.")) {
            fetchJSON('/reset_cal', {}).then(() => {
                alert("NVS partitions erased successfully. Robot is now rebooting...");
            });
        }
    }

    function scan() {
        document.getElementById('status').innerText = 'Scanning Wi-Fi...';
        fetch('/scan').then(r => r.json()).then(d => {
            let s = document.getElementById('ssid');
            s.innerHTML = '<option value="">-- Select --</option>';
            d.forEach(n => { s.innerHTML += '<option value="'+n+'">'+n+'</option>'; });
            document.getElementById('status').innerText = 'Found ' + d.length + ' network(s)';
        }).catch(() => document.getElementById('status').innerText = 'Scan Error');
    }

    function saveWiFi() {
        let s = document.getElementById('ssid').value;
        let p = document.getElementById('pass').value;
        if(!s) return alert('Select SSID');
        fetchJSON('/save', {ssid: s, pass: p}).then(() => alert('Saved. Rebooting...'));
    }

    function doAction(act) {
        if (allCalibrations[act]) {
            let p = allCalibrations[act];
            for (let leg in legMap) {
                let val = p[legMap[leg]];
                let el = document.getElementById(leg);
                if (el) el.value = val;
                let valEl = document.getElementById('val_' + leg);
                if (valEl) valEl.innerHTML = val + '&deg;';
            }
        }
        fetchJSON('/action', {action: act});
    }

    function moveServo(id, angle) {
        document.getElementById('val_' + id).innerHTML = angle + '&deg;';
        
        let payload = {};
        payload[legMap[id]] = parseInt(angle);
        
        if (syncEnabled) {
            let pairedId = null;
            if (id === 'high_left') pairedId = 'high_right';
            else if (id === 'high_right') pairedId = 'high_left';
            else if (id === 'low_left') pairedId = 'low_right';
            else if (id === 'low_right') pairedId = 'low_left';

            if (pairedId) {
                let pairedEl = document.getElementById(pairedId);
                if (pairedEl.value !== angle) {
                    pairedEl.value = angle;
                    document.getElementById('val_' + pairedId).innerHTML = angle + '&deg;';
                    payload[legMap[pairedId]] = parseInt(angle);
                }
            }
        }

        clearTimeout(servoTimeouts['bulk']);
        servoTimeouts['bulk'] = setTimeout(() => {
            fetchJSON('/servo', payload);
        }, 30);
    }

    function toggleSensor() {
        localSensorEnabled = !localSensorEnabled;
        let btn = document.getElementById('btn_sensor');
        if (localSensorEnabled) {
            btn.innerText = "Disable Sensor"; btn.classList.remove('btn-green'); btn.classList.add('btn-red');
        } else {
            btn.innerText = "Enable Sensor"; btn.classList.remove('btn-red'); btn.classList.add('btn-green');
        }
        sendSensorConfig();
    }

    function updateThreshold(val) {
        document.getElementById('val_threshold').innerText = val + "cm";
        sendSensorConfig();
    }

    function updateReaction(val) {
        document.getElementById('val_reaction').innerText = val + "ms";
        sendSensorConfig();
    }

    function sendSensorConfig() {
        let thresh = parseInt(document.getElementById('threshold').value) || 20;
        let react = parseInt(document.getElementById('reaction').value) || 0;
        let tripAct = document.getElementById('sensor_tripped_action').value;
        let clearAct = document.getElementById('sensor_cleared_action').value;
        
        let tripAudEl = document.getElementById('sensor_tripped_audio');
        let clearAudEl = document.getElementById('sensor_cleared_audio');

        fetchJSON('/sensor', {
            enabled: localSensorEnabled, 
            threshold: thresh,
            reaction_time: react,
            tripped_action: tripAct,
            cleared_action: clearAct,
            tripped_audio: tripAudEl ? tripAudEl.value : 'none',
            cleared_audio: clearAudEl ? clearAudEl.value : 'none'
        });
    }

    function updateStatus() {
        fetch('/angles')
            .then(r => r.json())
            .then(data => {
                let srvCard = document.getElementById('servo_card');
                let srvSliders = document.querySelectorAll('.srv-slider');
                
                if (data.safety_lock) {
                    document.getElementById('lock_banner').style.display = 'block';
                    srvCard.classList.add('locked-mode');
                    srvSliders.forEach(slider => slider.disabled = true);
                } else {
                    document.getElementById('lock_banner').style.display = 'none';
                    srvCard.classList.remove('locked-mode');
                    srvSliders.forEach(slider => slider.disabled = false);
                }

                for (let [leg, stats] of Object.entries(data)) {
                    if (typeof stats === 'object') {
                        let el = document.getElementById(leg);
                        if(el && (!isDragging(leg) || data.safety_lock)) { 
                            el.value = stats.angle; 
                            document.getElementById('val_' + leg).innerHTML = stats.angle + '&deg;'; 
                        }
                    }
                }

                let liveSpan = document.getElementById('live_dist');
                if (data.sensor_enabled) {
                    liveSpan.innerText = data.sensor_distance >= 0 ? data.sensor_distance.toFixed(1) : "Out of Range";
                    liveSpan.style.color = (data.sensor_distance > 0 && data.sensor_distance < data.sensor_threshold) ? "#ef4444" : "#10b981";
                } else {
                    liveSpan.innerText = "Disabled";
                    liveSpan.style.color = "#94a3b8";
                }

                if (document.activeElement !== document.getElementById('btn_sensor')) {
                    localSensorEnabled = data.sensor_enabled;
                    let btn = document.getElementById('btn_sensor');
                    if (data.sensor_enabled) {
                        btn.innerText = "Disable Sensor"; btn.className = "btn-red";
                    } else {
                        btn.innerText = "Enable Sensor"; btn.className = "btn-green";
                    }
                }
                
                let vSlider = document.getElementById('volume_slider');
                if (vSlider && document.activeElement !== vSlider && data.audio_volume !== undefined) {
                    vSlider.value = data.audio_volume;
                    let valVol = document.getElementById('val_volume');
                    if(valVol) valVol.innerText = data.audio_volume + "%";
                }

                if (data.audio_bclk !== undefined) {
                    updatePinButtons(data.audio_bclk, data.audio_lrc);
                }

                if (document.activeElement !== document.getElementById('threshold')) {
                    document.getElementById('threshold').value = data.sensor_threshold;
                    document.getElementById('val_threshold').innerText = data.sensor_threshold + "cm";
                }

                if (document.activeElement !== document.getElementById('reaction')) {
                    document.getElementById('reaction').value = data.sensor_reaction_time || 0;
                    document.getElementById('val_reaction').innerText = (data.sensor_reaction_time || 0) + "ms";
                }

                if (document.activeElement !== document.getElementById('sensor_tripped_action')) {
                    document.getElementById('sensor_tripped_action').value = data.sensor_tripped_action || 'stop';
                }
                if (document.activeElement !== document.getElementById('sensor_cleared_action')) {
                    document.getElementById('sensor_cleared_action').value = data.sensor_cleared_action || 'stand';
                }
                
                let stAud = document.getElementById('sensor_tripped_audio');
                if (stAud && document.activeElement !== stAud) {
                    stAud.value = data.sensor_tripped_audio || 'none';
                }
                let scAud = document.getElementById('sensor_cleared_audio');
                if (scAud && document.activeElement !== scAud) {
                    scAud.value = data.sensor_cleared_audio || 'none';
                }
                
                setTimeout(updateStatus, 800); 
            })
            .catch(err => {
                setTimeout(updateStatus, 2000); 
            });
    }

    window.onload = function() { 
        fetchCalibrations();
        updateStatus(); 
    }
</script>
</body>
</html>
)raw_html";

const char HTML_ROBOT_BASIC[] = R"raw_html(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Robot Diagnostics</title>
<style>
  body{background:#020617;color:#fff;font-family:sans-serif;padding:20px;max-width:500px;margin:auto;}
  .card{background:#0f172a;padding:20px;border-radius:10px;margin-bottom:20px;border:1px solid #1e293b;}
  button,select,input{width:100%;padding:12px;margin:5px 0;border-radius:8px;border:none;background:#1e293b;color:#fff;}
  button{background:#2563eb;font-weight:bold;cursor:pointer;margin-top:10px;}
  h2{color:#38bdf8;}
</style>
</head><body>
<h2>ESP32 Robot Diagnostics</h2>
<div class="card">
  <button onclick="fetch('/beep')">Test Speaker Tone</button>
  <button onclick="fetch('/action',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'stand'})})">Stand Up</button>
  <button onclick="fetch('/action',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'sit'})})">Sit Down</button>
</div>
<div class="card">
  <h3 style="margin-top:0;">Wi-Fi Setup</h3>
  <button onclick="fetch('/scan').then(r=>r.json()).then(d=>{let s=document.getElementById('ssid');s.innerHTML='';d.forEach(n=>s.innerHTML+='<option>'+n+'</option>');})">Scan Networks</button>
  <select id="ssid"><option>Scan first...</option></select>
  <input type="password" id="pass" placeholder="Wi-Fi Password">
  <button style="background:#10b981;" onclick="fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.getElementById('ssid').value,pass:document.getElementById('pass').value})}).then(()=>alert('Rebooting...'))">Save & Connect</button>
</div>
</body></html>
)raw_html";
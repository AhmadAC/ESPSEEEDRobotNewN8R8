// main\web_html_robot.cpp
#include "web_html.h"

// -------------------------------------------------------------
// DYNAMIC ROBOT HTML PARTITIONS
// -------------------------------------------------------------
const char html_part1[] = R"raw_html(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESPRobot Dashboard</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
        :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
        body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; margin: 0; }
        .container { max-width: 800px; margin: 0 auto; }
        h1 { text-align: center; color: var(--primary); margin-bottom: 30px; }
        .card { background: var(--card); border-radius: 16px; padding: 25px; border: 1px solid #334155; margin-bottom: 25px; box-sizing: border-box; }
        h2 { border-bottom: 1px solid #334155; padding-bottom: 10px; color: var(--primary); margin-top: 0; margin-bottom: 15px; }
        .grid { display: grid; grid-template-columns: 1fr; gap: 20px; }
        @media (min-width: 600px) { .grid { grid-template-columns: 1fr 1fr; } }
        
        .ctrl-group { background: #0f172a; padding: 15px; border-radius: 12px; border: 1px solid #334155; border-left: 4px solid #3b82f6; transition: opacity 0.3s; }
        .ctrl-header { display: flex; justify-content: space-between; font-weight: bold; margin-bottom: 8px; color: #94a3b8; font-size: 0.9rem;}
        label { display: block; margin-bottom: 8px; font-weight: bold; font-size: 0.9rem; color: #cbd5e1; }
        
        input[type=range] { width: 100%; margin-bottom: 15px; cursor: pointer; accent-color: var(--primary); }
        input[type=range]:disabled { cursor: not-allowed; opacity: 0.5; }
        
        button { background: #3b82f6; color: white; border: none; padding: 12px 15px; border-radius: 10px; cursor: pointer; font-size: 15px; width: 100%; transition: transform 0.1s, opacity 0.2s; box-sizing: border-box; font-weight: bold; }
        button:hover { opacity: 0.9; }
        button:active { transform: scale(0.96); opacity: 0.8; }
        button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
        
        select, input[type=password], input[type=number] { width: 100%; padding: 12px; box-sizing: border-box; border: 1px solid #475569; border-radius: 10px; background: #0f172a; color: white; margin-bottom: 15px; }
        .pass-container { display: flex; gap: 10px; align-items: center; margin-bottom: 15px; }
        .pass-container input[type=password], .pass-container input[type=text] { margin-bottom: 0; flex-grow: 1; }
        .pass-container input[type=checkbox] { width: auto; margin: 0; }
        
        #status { font-weight: bold; color: #10b981; text-align: center; margin-top: 10px; font-size: 0.9rem; }
        #lock_banner { display: none; background: #7f1d1d; color: #fca5a5; padding: 12px; border-radius: 10px; text-align: center; font-weight: bold; margin-bottom: 20px; border: 1px solid #ef4444; }
        
        .locked-mode .ctrl-group, .locked-mode .btn-grid button, .locked-mode .sync-header { opacity: 0.5; pointer-events: none; }
        .btn-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 10px; margin-bottom: 25px; }
        .sync-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }
        
        .btn-green { background: #10b981; }
        .btn-teal { background: #14b8a6; }
        .btn-purple { background: #8b5cf6; }
        .btn-orange { background: #f59e0b; }
        .btn-red { background: #ef4444; }
        .btn-gray { background: #475569; }
        .btn-dark { background: #1e293b; border: 1px solid #334155; }
        
        .text-sm { font-size: 0.85rem; color: #94a3b8; margin-top: 0; }
        .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-align: center; font-size: 0.85rem; border: 1px solid; text-transform: uppercase; background: #172554; color: #93c5fd; border-color: #3b82f6; margin-bottom: 20px; }
        
        /* Gamepad Custom CSS */
        .gp-header { border-bottom: 1px solid #334155; padding-bottom: 10px; margin-bottom: 15px; }
        .gp-title { font-size: 1.1rem; font-weight: bold; color: #fff; word-break: break-word; }
        .gp-meta { font-size: 0.8rem; color: #94a3b8; margin-top: 4px; }
        .detected-badge { display: inline-block; background: rgba(14, 165, 233, 0.2); color: var(--primary); border: 1px solid var(--primary); font-size: 0.75rem; padding: 2px 8px; border-radius: 4px; margin-top: 6px; font-weight: bold; }
        .section-title { font-size: 0.9rem; text-transform: uppercase; letter-spacing: 1px; color: var(--primary); margin: 15px 0 10px 0; }
        .buttons-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(85px, 1fr)); gap: 10px; }
        .btn-card { background: #0f172a; border: 1px solid #334155; border-radius: 8px; padding: 8px 5px; text-align: center; transition: all 0.05s ease; display: flex; flex-direction: column; justify-content: center; align-items: center; min-height: 60px; }
        .btn-card.pressed { background: #10b981; border-color: #10b981; color: #000; box-shadow: 0 0 10px rgba(16, 185, 129, 0.5); transform: scale(1.05); }
        .btn-label { font-size: 0.9rem; font-weight: bold; line-height: 1.1; }
        .btn-id { font-size: 0.75rem; opacity: 0.8; margin-top: 2px; }
        .btn-val { font-size: 0.65rem; opacity: 0.6; margin-top: 2px; font-family: monospace; }
        .axes-container { display: flex; flex-direction: column; gap: 10px; }
        .axis-row { display: flex; align-items: center; gap: 10px; background: #0f172a; padding: 8px 12px; border-radius: 8px; border: 1px solid #334155; }
        .axis-label { font-size: 0.8rem; font-weight: bold; width: 55px; }
        .axis-bar-bg { flex-grow: 1; height: 12px; background: #1e293b; border-radius: 6px; position: relative; overflow: hidden; }
        .axis-bar-fill { position: absolute; top: 0; bottom: 0; background: var(--primary); width: 0%; transition: width 0.05s linear, left 0.05s linear; }
        .axis-value { font-size: 0.8rem; font-family: monospace; width: 45px; text-align: right; }
        .no-gamepad { text-align: center; padding: 40px 20px; background: var(--card); border-radius: 12px; border: 1px dashed #475569; }
        .no-gamepad h2 { font-size: 1.2rem; margin-bottom: 10px; color: #fff; }
        .no-gamepad p { font-size: 0.85rem; color: #94a3b8; line-height: 1.4; }
        .pulse { display: inline-block; width: 10px; height: 10px; background-color: var(--primary); border-radius: 50%; margin-right: 6px; animation: blink 1.5s infinite; }
        @keyframes blink { 0% { opacity: 0.2; } 50% { opacity: 1; } 100% { opacity: 0.2; } }
    </style>
</head>
<body>
<div class='container'>
    <div class="status-bar">ESPRobot Control Dashboard</div>

    <div class='card'>
        <h2>Wi-Fi Provisioning</h2>
        <div class='grid'>
            <div>
                <button class='btn-gray' onclick='scan()'>Scan Wi-Fi Networks</button>
                <p id='status'>Ready to Scan</p>
            </div>
            <div>
                <label>Target SSID:</label>
                <select id='ssid'><option value=''>-- Select --</option></select>
                <label>Wi-Fi Password:</label>
                <div class='pass-container'>
                    <input type='password' id='pass' placeholder='Enter password'>
                    <input type='checkbox' onclick="let p=document.getElementById('pass'); p.type=(p.type==='password')?'text':'password';"> <small class="text-sm">Show</small>
                </div>
                <button class='btn-green' onclick='saveWiFi()'>Save and Connect</button>
            </div>
        </div>
        <hr style='border-color:#334155; margin: 25px 0;'>
        <div class='text-sm' style='margin-bottom:10px;'>Quick Boot Mode Switch</div>
        <div class='grid'>
            <button class='btn-orange' onclick='forceAP()'>Force AP Mode</button>
            <button class='btn-green' onclick='forceWiFi()'>Use Saved Wi-Fi</button>
        </div>
        <hr style='border-color:#334155; margin: 25px 0;'>
        <div class='text-sm' style='margin-bottom:10px;'>Hardware Profile (Reboot Required)</div>
        <div class='grid'>
            <button class='btn-gray' onclick='setMode("claw")'>Claw Mode</button>
            <button class='btn-gray' onclick='setMode("cam")'>Camera Mode</button>
            <button class='btn-purple' disabled>✓ Robot Mode</button>
        </div>
    </div>
)raw_html";

const char html_cam_card[] = R"raw_html(
    <!-- CAMERA CARD -->
    <div class='card' id='robot_cam_card'>
        <h2>Live Camera</h2>
        <div class="stream-container" id="streamContainer" style="display:none; width: 100%; border-radius: 8px; overflow: hidden; background-color: #000; margin-bottom: 15px; min-height: 200px; position: relative; display: flex; align-items: center; justify-content: center;">
            <img id="streamImg" alt="Live Stream" style="display: block; width: 100%; height: auto; transition: transform 0.2s;">
        </div>
        <div id="camControls" style="display:none; margin-bottom: 15px;">
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                <button id="snapBtn" class="btn-green" onclick="takeSnapshot()">Save HD Picture</button>
                <button class="btn-orange" onclick="rotateCamera()">Rotate 90&deg;</button>
            </div>
            <div id="camStatus" class="text-sm" style="margin-top: 10px; text-align:center;">Streaming live...</div>
        </div>
        <button id="camToggleBtn" class="btn-blue" onclick="toggleCamera()">Turn Camera ON</button>
    </div>
)raw_html";

const char html_audio_card[] = R"raw_html(
    <!-- AUDIO CARD -->
    <div class='card'>
        <h2>Audio & Speaker Output</h2>
        <div class='grid'>
            <div class='ctrl-group' style='border-left-color: #f43f5e;'>
                <div class='ctrl-header'><span>Volume Control (Software PCM)</span><span id='val_volume'>50%</span></div>
                <input type='range' id='volume_slider' min='0' max='100' value='50' onchange='updateVolume(this.value)'>
                <p class='text-sm' style='margin-bottom:0;'>Controls safe digital amplifier output level.</p>
            </div>
            <div class='ctrl-group' style='border-left-color: #10b981;'>
                <label>Test Digital Audio Stream:</label>
                <select id='audio_test_file'>
                    <option value='dog_bark'>dog-barking.wav</option>
                </select>
                <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 10px;'>
                    <button class='btn-teal' onclick='testAudio()'>Play</button>
                    <button class='btn-red' onclick='stopAudio()'>Stop Sound</button>
                </div>
                <button class='btn-purple' style='margin-top: 10px;' onclick='recordAudio(this)'>Record & Play (3s)</button>
                
                <hr style='border-color:#334155; margin: 15px 0;'>
                <label>Live Microphone Stream (Wi-Fi/AP):</label>
                <audio id='mic_stream' controls style='width: 100%; height: 40px; margin-top: 5px; border-radius: 8px;'></audio>
                <button id='btn_mic_stream' class='btn-blue' style='margin-top: 10px;' onclick='toggleMicStream()'>Start Live Mic</button>
            </div>
        </div>
    </div>
)raw_html";

const char html_part2[] = R"raw_html(
    <div class='card' id='servo_card'>
        <h2>Robot Actions and Kinematics</h2>
        <div id='lock_banner'>WARNING: MOTORS LOCKED - Obstacle Detected!</div>
        
        <div class='btn-grid'>
            <button onclick='doAction("forward")'>Walk Forward</button>
            <button onclick='doAction("backward")'>Walk Backward</button>
            <button class='btn-teal' onclick='doAction("step_forward")'>Step Forward</button>
            <button class='btn-teal' onclick='doAction("step_backward")'>Step Backward</button>
            <button class='btn-teal' onclick='doAction("leap_forward")'>Leap Forward</button>
            <button class='btn-purple' onclick='doAction("left_wave")'>Left Wave</button>
            <button class='btn-purple' onclick='doAction("right_wave")'>Right Wave</button>
            <button class='btn-purple' onclick='doAction("back_left_wave")'>Back Left Wave</button>
            <button class='btn-purple' onclick='doAction("back_right_wave")'>Back Right Wave</button>
            <button class='btn-orange' onclick='doAction("crawl")'>Forward Crawl</button>
            <button class='btn-red' onclick='doAction("stop")'>STOP</button>
            <button class='btn-orange' onclick='doAction("sit")'>Sit</button>
            <button class='btn-orange' onclick='doAction("stand")'>Stand Up</button>
            <button class='btn-purple' onclick='doAction("stretch_down")'>Stretch Down</button>
            <button class='btn-purple' onclick='doAction("stretch_back")'>Stretch Back</button>
        </div>

        <div class='sync-header'>
            <h3 style='margin:0; font-size: 16px; color: var(--primary);'>Manual Joint Control</h3>
            <button id='btn_sync' onclick='toggleSync()' class='btn-gray' style='width: auto; padding: 8px 15px; margin: 0;'>Sync Legs: OFF</button>
        </div>

        <div class='grid'>
            <div class='ctrl-group'>
                <div class='ctrl-header'><span>Low Left Leg (IO12)</span><span id='val_low_left'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='low_left' min='0' max='180' value='90' oninput='moveServo("low_left", this.value)' onmousedown='dragStart("low_left")' onmouseup='dragEnd()' ontouchstart='dragStart("low_left")' ontouchend='dragEnd()'>
            </div>
            <div class='ctrl-group'>
                <div class='ctrl-header'><span>High Left Shoulder (IO11)</span><span id='val_high_left'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='high_left' min='0' max='180' value='90' oninput='moveServo("high_left", this.value)' onmousedown='dragStart("high_left")' onmouseup='dragEnd()' ontouchstart='dragStart("high_left")' ontouchend='dragEnd()'>
            </div>
            <div class='ctrl-group'>
                <div class='ctrl-header'><span>Low Right Leg (IO9)</span><span id='val_low_right'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='low_right' min='0' max='180' value='90' oninput='moveServo("low_right", this.value)' onmousedown='dragStart("low_right")' onmouseup='dragEnd()' ontouchstart='dragStart("low_right")' ontouchend='dragEnd()'>
            </div>
            <div class='ctrl-group'>
                <div class='ctrl-header'><span>High Right Shoulder (IO10)</span><span id='val_high_right'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='high_right' min='0' max='180' value='90' oninput='moveServo("high_right", this.value)' onmousedown='dragStart("high_right")' onmouseup='dragEnd()' ontouchstart='dragStart("high_right")' ontouchend='dragEnd()'>
            </div>
        </div>
    </div>

    <div class='card'>
        <h2>Pose Calibration & Zeroing</h2>
        <div class='grid'>
            <div class='ctrl-group' style='border-left-color: #f59e0b;'>
                <label>Target to Calibrate:</label>
                <select id='calib_target' onchange='loadCalibTarget()' style='font-weight:bold; cursor:pointer;'>
                    <option value='offsets'>1. Hardware Zeroing (Offsets)</option>
                    <option value='sit'>2. Pose: Sit</option>
                    <option value='stand'>3. Pose: Stand Up</option>
                    <option value='stretch_down'>4. Pose: Stretch Down</option>
                    <option value='stretch_back'>5. Pose: Stretch Back</option>
                    <option value='stop'>6. Pose: Stop (Default)</option>
                </select>
                <p class='text-sm' style='margin-top:10px; line-height:1.4;'>First, use Hardware Zeroing to make all motors physically 90&deg;. Then calibrate limits for buttons.</p>
                <button class='btn-red' style='margin-top: 15px;' onclick='resetCal()'>Reset NVS to Firmware Defaults</button>
            </div>
            <div class='ctrl-group' style='border-left-color: #94a3b8; text-align:center;'>
                <p style='margin-top:0; font-size:13px; font-weight:bold;'>Compile Defaults into Firmware</p>
                <p class='text-sm' style='line-height:1.4;'>Save your finalized configuration to a C++ file to hardcode settings securely for your next compile.</p>
                <button class='btn-dark' style='margin-top: 10px;' onclick='downloadCalib()'>Download C++ Config</button>
            </div>
        </div>
        <div class='grid' style='margin-top:15px;'>
            <div>
                <label>Low Left Motor Limit:</label>
                <input type='number' id='cal_ll' value='0'>
                <label>Low Right Motor Limit:</label>
                <input type='number' id='cal_lr' value='0'>
            </div>
            <div>
                <label>High Left Motor Limit:</label>
                <input type='number' id='cal_hl' value='0'>
                <label>High Right Motor Limit:</label>
                <input type='number' id='cal_hr' value='0'>
            </div>
        </div>
        <div class='grid' style='margin-top:15px;'>
            <button class='btn-blue' onclick='testCalib()'>Test Pose or Offset</button>
            <button class='btn-orange' onclick='saveCalib()'>Save to Robot NVS Storage</button>
        </div>
    </div>

    <div class='card'>
        <h2>Ultrasonic Obstacle Safety Monitor</h2>
        <div class='grid'>
            <div class='ctrl-group' style='border-left-color: #ef4444;'>
                <label>Sensor Toggle Status:</label>
                <button id='btn_sensor' onclick='toggleSensor()'>Enable Sensor</button>
                <p style='font-size: 16px; margin-top: 15px;'>Live Distance: <span id='live_dist' style='font-weight: bold; color: var(--primary);'>--</span> cm</p>
            </div>
            <div class='ctrl-group' style='border-left-color: #8b5cf6;'>
                <div class='ctrl-header'><span>Safety Halt Threshold</span><span id='val_threshold'>20cm</span></div>
                <input type='range' id='threshold' min='5' max='100' value='20' onchange='updateThreshold(this.value)'>
                <p class='text-sm' style='margin-bottom: 15px; line-height: 1.4;'>Triggers immediately when distance crosses limit.</p>
                
                <div class='ctrl-header'><span>Reaction Delay</span><span id='val_reaction'>0ms</span></div>
                <input type='range' id='reaction' min='0' max='5000' step='100' value='0' onchange='updateReaction(this.value)'>
                <p class='text-sm' style='line-height: 1.4;'>Wait time between detection and automatic action execution.</p>
            </div>
        </div>
        <div class='grid' style='margin-top:15px;'>
            <div class='ctrl-group' style='border-left-color: #f59e0b;'>
                <label>Physical Action When Tripped:</label>
                <select id='sensor_tripped_action' onchange='sendSensorConfig()'>
                    <option value='stop'>Stop (Default)</option>
                    <option value='forward'>Walk Forward</option>
                    <option value='backward'>Walk Backward</option>
                    <option value='step_forward'>Step Forward</option>
                    <option value='step_backward'>Step Backward</option>
                    <option value='leap_forward'>Leap Forward</option>
                    <option value='left_wave'>Left Wave</option>
                    <option value='right_wave'>Right Wave</option>
                    <option value='back_left_wave'>Back Left Wave</option>
                    <option value='back_right_wave'>Back Right Wave</option>
                    <option value='crawl'>Forward Crawl</option>
                    <option value='sit'>Sit</option>
                    <option value='stand'>Stand Up</option>
                    <option value='stretch_down'>Stretch Down</option>
                    <option value='stretch_back'>Stretch Back</option>
                    <option value='none'>None (Do nothing)</option>
                </select>
)raw_html";

const char html_audio_tripped[] = R"raw_html(
                <label style='margin-top:15px;'>Audio Sound When Tripped:</label>
                <select id='sensor_tripped_audio' onchange='sendSensorConfig()'>
                    <option value='none'>Mute</option>
                    <option value='dog_bark'>Dog Bark</option>
                </select>
)raw_html";

const char html_part3[] = R"raw_html(
            </div>
            <div class='ctrl-group' style='border-left-color: #10b981;'>
                <label>Physical Action When Cleared:</label>
                <select id='sensor_cleared_action' onchange='sendSensorConfig()'>
                    <option value='stand'>Stand Up</option>
                    <option value='forward'>Walk Forward</option>
                    <option value='backward'>Walk Backward</option>
                    <option value='step_forward'>Step Forward</option>
                    <option value='step_backward'>Step Backward</option>
                    <option value='leap_forward'>Leap Forward</option>
                    <option value='left_wave'>Left Wave</option>
                    <option value='right_wave'>Right Wave</option>
                    <option value='back_left_wave'>Back Left Wave</option>
                    <option value='back_right_wave'>Back Right Wave</option>
                    <option value='crawl'>Forward Crawl</option>
                    <option value='stop'>Stop</option>
                    <option value='sit'>Sit</option>
                    <option value='stretch_down'>Stretch Down</option>
                    <option value='stretch_back'>Stretch Back</option>
                    <option value='none'>None (Do nothing)</option>
                </select>
)raw_html";

const char html_audio_cleared[] = R"raw_html(
                <label style='margin-top:15px;'>Audio Sound When Cleared:</label>
                <select id='sensor_cleared_audio' onchange='sendSensorConfig()'>
                    <option value='none'>Mute</option>
                    <option value='dog_bark'>Dog Bark</option>
                </select>
)raw_html";

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
                <p>Connect your Gamepad via Bluetooth/USB to this device and <strong>press any button</strong> to wake it up.</p>
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
            // UI Sync
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
                    if (aIndex === 1 || aIndex === 3) { // Trigger movement on vertical axes Y
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
    // ---------------------------------

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
        let el = document.getElementById('audio_test_file');
        if(el) fetchJSON('/audio_play', { sound: el.value });
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
// main/web_html_robot_peripherals.cpp
#include "web_html.h"

// Camera live stream and snapshot card
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

// HT517 I2S speaker, mic controls, and volume interface
const char html_audio_card[] = R"raw_html(
    <!-- AUDIO CARD -->
    <div class='card'>
        <h2>Audio & Speaker Output (HT517 I2S)</h2>
        <div class='grid'>
            <div class='ctrl-group' style='border-left-color: #f43f5e;'>
                <div class='ctrl-header'><span>Volume Control (Software PCM)</span><span id='val_volume'>80%</span></div>
                <input type='range' id='volume_slider' min='0' max='100' value='80' onchange='updateVolume(this.value)'>
                <p class='text-sm' style='margin-bottom:12px;'>Controls safe digital amplifier output level.</p>
                
                <label style='font-size:0.8rem;'>Hardware Pin Preset (Tap to Test):</label>
                <div style='display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 6px; margin-top: 5px;'>
                    <button id='btnPinYD' class='btn-pin active' onclick='applyPins(10, 11, 12)'>1: 10,11,12</button>
                    <button id='btnPinSeeed' class='btn-pin' onclick='applyPins(8, 7, 9)'>2: 8,7,9</button>
                    <button id='btnPinSwap' class='btn-pin' onclick='applyPins(12, 11, 10)'>3: 12,11,10</button>
                </div>
            </div>
            <div class='ctrl-group' style='border-left-color: #10b981;'>
                <label>Test Digital Audio Output:</label>
                <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 8px;'>
                    <button class='btn-teal' onclick='testAudio()'>Play Bark</button>
                    <button class='btn-blue' onclick='playBeep()'>Test Tone</button>
                </div>
                <button class='btn-red' style='margin-top: 10px;' onclick='stopAudio()'>Stop Sound</button>
                <button class='btn-purple' style='margin-top: 10px;' onclick='recordAudio(this)'>Record & Play (3s)</button>
                
                <hr style='border-color:#334155; margin: 15px 0;'>
                <label>Live Microphone Stream (Wi-Fi/AP):</label>
                <audio id='mic_stream' controls style='width: 100%; height: 40px; margin-top: 5px; border-radius: 8px;'></audio>
                <button id='btn_mic_stream' class='btn-blue' style='margin-top: 10px;' onclick='toggleMicStream()'>Start Live Mic</button>
            </div>
        </div>
    </div>
)raw_html";
// main/web_html_robot_controls.cpp
#include "web_html.h"

// Actions, Kinematics, Servo Sliders, Pose Zeroing & Ultrasonic Safety Cards
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
                <div class='ctrl-header'><span>Low Left Leg (GPIO 1)</span><span id='val_low_left'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='low_left' min='0' max='180' value='90' oninput='moveServo("low_left", this.value)' onmousedown='dragStart("low_left")' onmouseup='dragEnd()' ontouchstart='dragStart("low_left")' ontouchend='dragEnd()'>
            </div>
            <div class='ctrl-group'>
                <div class='ctrl-header'><span>High Left Shoulder (GPIO 3)</span><span id='val_high_left'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='high_left' min='0' max='180' value='90' oninput='moveServo("high_left", this.value)' onmousedown='dragStart("high_left")' onmouseup='dragEnd()' ontouchstart='dragStart("high_left")' ontouchend='dragEnd()'>
            </div>
            <div class='ctrl-group'>
                <div class='ctrl-header'><span>Low Right Leg (GPIO 4)</span><span id='val_low_right'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='low_right' min='0' max='180' value='90' oninput='moveServo("low_right", this.value)' onmousedown='dragStart("low_right")' onmouseup='dragEnd()' ontouchstart='dragStart("low_right")' ontouchend='dragEnd()'>
            </div>
            <div class='ctrl-group'>
                <div class='ctrl-header'><span>High Right Shoulder (GPIO 2)</span><span id='val_high_right'>90&deg;</span></div>
                <input type='range' class='srv-slider' id='high_right' min='0' max='180' value='90' oninput='moveServo("high_right", this.value)' onmousedown='dragStart("high_right")' onmouseup='dragEnd()' ontouchstart='dragStart("high_right")' ontouchend='dragEnd()'>
            </div>
        </div>
    </div>

    <!-- CALIBRATION & HARDWARE ZEROING CARD (RESTORED) -->
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

    <!-- ULTRASONIC OBSTACLE MONITOR CARD (RESTORED WITH LIVE STREAMING) -->
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
                    <option value='test_beep'>Test Tone</option>
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
                    <option value='test_beep'>Test Tone</option>
                </select>
)raw_html";
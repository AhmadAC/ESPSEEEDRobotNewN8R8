// main/web_html_robot_layout.cpp
#include "web_html.h"

// Part 1: Document Head, CSS Design System, and Wi-Fi Provisioning Card
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
        .btn-pin { background: #1e293b; border: 1px solid #475569; padding: 8px 6px; font-size: 0.75rem; color: #cbd5e1; border-radius: 8px; }
        .btn-pin.active { background: #0284c7; border-color: #38bdf8; color: #fff; font-weight: bold; }
        
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
    <div class="status-bar">ESPRobot Control Dashboard (ESP32-S3 Zero)</div>

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
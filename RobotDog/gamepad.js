const SWITCH_BUTTON_NAMES = [
  "B", "A", "Y", "X",
  "L", "R", "ZL", "ZR",
  "-", "+", "L3", "R3",
  "D-Up", "D-Down", "D-Left", "D-Right"
];

const DEFAULT_SWITCH_MAP = {
  0: "stop",
  1: "stand",
  2: "sit",
  3: "bark",
  4: "beep",
  5: "stop",
  6: "vol -10",
  7: "vol +10",
  8: "stretch_back",
  9: "stretch_down",
  10: "crawl",
  11: "leap_forward",
  12: "forward",
  13: "backward",
  14: "left_wave",
  15: "right_wave"
};

let switchKeyMap = {};
let selectedSwitchIndex = 0;
let isRemapMode = false;
let autoRetractEnabled = true;
let wasJoystickActive = false;
let lastBtnStates = {};

function initSwitchButtonMap() {
  try {
    const stored = localStorage.getItem("esp32_switch_map");
    switchKeyMap = stored ? JSON.parse(stored) : Object.assign({}, DEFAULT_SWITCH_MAP);
  } catch (e) {
    switchKeyMap = Object.assign({}, DEFAULT_SWITCH_MAP);
  }
  updateAllSwitchButtonLabels();
  selectSwitchButton(0);
}

function updateAllSwitchButtonLabels() {
  for (let i = 0; i < 16; i++) {
    const lbl = document.getElementById(`gp-lbl-${i}`);
    if (lbl) lbl.innerText = switchKeyMap[i] || "-";
  }
}

function selectSwitchButton(idx) {
  selectedSwitchIndex = idx;
  for (let i = 0; i < 16; i++) {
    const card = document.getElementById(`gp-btn-${i}`);
    if (card) {
      if (i === idx && isRemapMode) card.classList.add("selected-btn");
      else card.classList.remove("selected-btn");
    }
  }
  const name = SWITCH_BUTTON_NAMES[idx] || `Btn ${idx}`;
  const titleElem = document.getElementById("switch-editor-title");
  if (titleElem) titleElem.innerText = `Button ${name} Configuration`;

  const currentCmd = switchKeyMap[idx] || "";
  const input = document.getElementById("switch-cmd-input");
  const select = document.getElementById("switch-preset-select");
  if (input) input.value = currentCmd;
  if (select) select.value = currentCmd;
}

function onSelectSwitchPreset(val) {
  if (!val) return;
  const input = document.getElementById("switch-cmd-input");
  if (input) input.value = val;
}

async function saveSwitchButtonMapping() {
  const input = document.getElementById("switch-cmd-input");
  if (!input) return;
  const cmd = input.value.trim();
  if (!cmd) { showToast("Command cannot be empty.", "error"); return; }

  switchKeyMap[selectedSwitchIndex] = cmd;
  try { localStorage.setItem("esp32_switch_map", JSON.stringify(switchKeyMap)); } catch (e) {}
  updateAllSwitchButtonLabels();

  await sendCommand(`switchkey ${selectedSwitchIndex} ${cmd}`);
  showToast(`Button ${SWITCH_BUTTON_NAMES[selectedSwitchIndex]} mapped to: ${cmd}`, "success");
  appendLog("Switch", `Button ${SWITCH_BUTTON_NAMES[selectedSwitchIndex]} mapped -> '${cmd}'`);
}

async function testActiveSwitchButtonCommand() {
  const input = document.getElementById("switch-cmd-input");
  if (!input) return;
  const cmd = input.value.trim();
  if (!cmd) return;
  await sendCommand(cmd);
  appendLog("Test", `Triggered: '${cmd}'`);
}

async function requestSyncSwitchButtons() {
  showToast("Syncing button matrix from ESP32...", "info");
  await sendCommand("getswitchkeys");
  await sendCommand("status");
}

async function resetSwitchButtonDefaults() {
  switchKeyMap = Object.assign({}, DEFAULT_SWITCH_MAP);
  try { localStorage.setItem("esp32_switch_map", JSON.stringify(switchKeyMap)); } catch (e) {}
  updateAllSwitchButtonLabels();
  selectSwitchButton(selectedSwitchIndex);
  await sendCommand("switchreset");
  showToast("Switch mappings restored to defaults", "info");
  appendLog("Switch", "Restored Switch buttons to default.");
}

function toggleRemapMode() {
  isRemapMode = !isRemapMode;
  const btn = document.getElementById("btn-touch-mode");
  const editor = document.getElementById("switch-editor-box");
  const hint = document.getElementById("gamepad-mode-hint");

  if (isRemapMode) {
    btn.innerText = "Tap Action: REMAP";
    btn.style.background = "#f59e0b";
    editor.style.display = "flex";
    hint.innerText = "Tap any button card below to edit its command.";
    selectSwitchButton(selectedSwitchIndex);
  } else {
    btn.innerText = "Tap Action: SEND";
    btn.style.background = "#0284c7";
    editor.style.display = "none";
    hint.innerText = "Tap any button to trigger it immediately.";
    for (let i = 0; i < 16; i++) {
      const card = document.getElementById(`gp-btn-${i}`);
      if (card) card.classList.remove("selected-btn");
    }
  }
}

function onSwitchButtonCardClick(idx) {
  if (isRemapMode) selectSwitchButton(idx);
  else {
    const card = document.getElementById(`gp-btn-${idx}`);
    if (card) { card.classList.add("pressed"); setTimeout(() => card.classList.remove("pressed"), 120); }
    handleButtonPress(idx);
  }
}

function toggleAutoRetract() {
  autoRetractEnabled = !autoRetractEnabled;
  const btn = document.getElementById("btn-toggle-retract");
  if (btn) {
    btn.innerText = autoRetractEnabled ? "Retract: ON" : "Retract: OFF";
    btn.style.background = autoRetractEnabled ? "#10b981" : "#475569";
  }
}

function handleButtonPress(btnIndex) {
  const cmd = switchKeyMap[btnIndex] || DEFAULT_SWITCH_MAP[btnIndex];
  if (!cmd) return;
  appendLog("Trigger", `Button ${SWITCH_BUTTON_NAMES[btnIndex] || btnIndex} -> '${cmd}'`);

  if (cmd.startsWith("vol ")) {
    const delta = parseInt(cmd.substring(4), 10);
    let nextV = currentVolumeNum + delta;
    if (nextV < 0) nextV = 0; if (nextV > 100) nextV = 100;
    updateVolDisplay(nextV);
    sendCommand(`vol ${nextV}`);
  } else {
    sendCommand(cmd);
  }
}

function pollGamepadThrottled() {
  setTimeout(pollGamepadThrottled, 30);
  const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
  let gp = null;
  for (let i = 0; i < gamepads.length; i++) { if (gamepads[i]) { gp = gamepads[i]; break; } }
  if (!gp) return;

  gp.buttons.forEach((btn, idx) => {
    const isPressed = btn.pressed;
    const btnCard = document.getElementById(`gp-btn-${idx}`);
    if (btnCard && !isRemapMode) {
      if (isPressed) btnCard.classList.add("pressed");
      else btnCard.classList.remove("pressed");
    }
    if (isPressed && !lastBtnStates[idx]) handleButtonPress(idx);
    lastBtnStates[idx] = isPressed;
  });

  if (gp.axes.length > 0) {
    const leftX = gp.axes[0] || 0;
    const leftY = (gp.axes.length > 1) ? gp.axes[1] : 0;
    updateAxisUI(0, leftX);
    const deadzone = 0.25;

    if (leftY < -deadzone) { wasJoystickActive = true; sendCommand("forward"); }
    else if (leftY > deadzone) { wasJoystickActive = true; sendCommand("backward"); }
    else if (leftX < -deadzone) { wasJoystickActive = true; sendCommand("left_wave"); }
    else if (leftX > deadzone) { wasJoystickActive = true; sendCommand("right_wave"); }
    else if (wasJoystickActive) {
      wasJoystickActive = false;
      if (autoRetractEnabled) sendCommand("stop");
    }
  }
  if (gp.axes.length > 2) updateAxisUI(2, gp.axes[2]);
}

function updateAxisUI(axisIdx, val) {
  const bar = document.getElementById(`axis-bar-${axisIdx}`);
  const txt = document.getElementById(`axis-val-${axisIdx}`);
  if (!bar || !txt) return;

  txt.innerText = val.toFixed(2);
  let fillLeft = "50%", fillWidth = "0%";
  if (val > 0) { fillLeft = "50%"; fillWidth = (val * 50) + "%"; }
  else if (val < 0) { fillLeft = (50 + (val * 50)) + "%"; fillWidth = (Math.abs(val) * 50) + "%"; }
  bar.style.left = fillLeft;
  bar.style.width = fillWidth;
}

window.addEventListener("gamepadconnected", (e) => {
  appendLog("Gamepad", `Connected: ${e.gamepad.id}`);
  showToast("Gamepad Connected", "success");
  document.getElementById("gp-badge").innerText = "BT Gamepad Connected";
  document.getElementById("gp-badge").style.color = "#34d399";
});

window.addEventListener("gamepaddisconnected", () => {
  appendLog("Gamepad", "Disconnected.");
  showToast("Gamepad Disconnected", "warn");
  document.getElementById("gp-badge").innerText = "No BT Gamepad";
  document.getElementById("gp-badge").style.color = "rgba(255,255,255,0.5)";
});
let legAngles = { hl: 90, hr: 90, ll: 90, lr: 90 };
let legOffsets = { hl: 0, hr: 0, ll: 0, lr: 11 };

let legNames = {
  hl: "Front Left (HL)",
  hr: "Front Right (HR)",
  ll: "Back Left (LL)",
  lr: "Back Right (LR)"
};

let hardwarePins = {
  srv_ll: 1,
  srv_hr: 2,
  srv_hl: 3,
  srv_lr: 4,
  sens_trig: 5,
  sens_echo: 6,
  aud_bclk: 10,
  aud_lrc: 11,
  aud_din: 12
};

let syncFrontActive = false;
let syncBackActive = false;
let syncAllActive = false;
let isOffsetMode = false;

let pendingLeg = {};
let legThrottleTimer = null;

function initLegStorage() {
  try {
    const off = localStorage.getItem("esp32_leg_offsets");
    if (off) legOffsets = Object.assign(legOffsets, JSON.parse(off));
    const ang = localStorage.getItem("esp32_leg_angles");
    if (ang) legAngles = Object.assign(legAngles, JSON.parse(ang));
    const nms = localStorage.getItem("esp32_leg_names");
    if (nms) legNames = Object.assign(legNames, JSON.parse(nms));
    const pins = localStorage.getItem("esp32_hw_pins");
    if (pins) hardwarePins = Object.assign(hardwarePins, JSON.parse(pins));
  } catch (e) {}
  updateAllLegUI();
}

function updateAllLegUI() {
  ["hl", "hr", "ll", "lr"].forEach(k => {
    const offEl = document.getElementById(`o${k}-val`);
    if (offEl) offEl.innerText = legOffsets[k];

    const sl = document.getElementById(`${k}-slider`);
    const hdrEl = document.getElementById(`${k}-header`);
    const displayName = legNames[k] || k.toUpperCase();

    if (isOffsetMode) {
      if (sl) { sl.classList.add("offset-slider"); sl.min = "-45"; sl.max = "45"; sl.value = legOffsets[k]; }
      if (hdrEl) hdrEl.innerHTML = `${displayName} Trim: <span id="${k}-val" style="color:#f59e0b; font-family:monospace; font-weight:700;">${legOffsets[k] > 0 ? '+' : ''}${legOffsets[k]}</span>`;
    } else {
      if (sl) { sl.classList.remove("offset-slider"); sl.min = "0"; sl.max = "180"; sl.value = legAngles[k]; }
      if (hdrEl) hdrEl.innerHTML = `${displayName}: <span id="${k}-val" style="color:#38bdf8; font-family:monospace; font-weight:700;">${legAngles[k]} deg</span>`;
    }
  });
}

function toggleOffsetMode() {
  isOffsetMode = !isOffsetMode;
  const btn = document.getElementById("offset-mode-btn");
  if (btn) {
    if (isOffsetMode) {
      btn.innerText = "Offset Mode: ON (TRIM)";
      btn.style.background = "#f59e0b";
      btn.style.color = "#000000";
      btn.style.borderColor = "#f59e0b";
      showToast("Offset Trim Mode Active", "warn");
    } else {
      btn.innerText = "Offset Mode: OFF";
      btn.style.background = "#1e293b";
      btn.style.color = "#f59e0b";
      btn.style.borderColor = "#f59e0b";
      showToast("Joint Control Mode Active", "info");
    }
  }
  updateAllLegUI();
}

function getSyncedLegs(primaryLeg) {
  if (syncAllActive) return ["hl", "hr", "ll", "lr"];
  if (syncFrontActive && (primaryLeg === "hl" || primaryLeg === "hr")) return ["hl", "hr"];
  if (syncBackActive && (primaryLeg === "ll" || primaryLeg === "lr")) return ["ll", "lr"];
  return [primaryLeg];
}

function onLegSlider(leg, val) {
  const parsedVal = parseInt(val, 10);
  const targetLegs = getSyncedLegs(leg);

  if (isOffsetMode) {
    targetLegs.forEach(k => {
      legOffsets[k] = parsedVal;
      const offEl = document.getElementById(`o${k}-val`);
      if (offEl) offEl.innerText = parsedVal;
      const valEl = document.getElementById(`${k}-val`);
      if (valEl) valEl.innerText = `${parsedVal > 0 ? '+' : ''}${parsedVal}`;
      const sl = document.getElementById(`${k}-slider`);
      if (sl && sl.value != parsedVal) sl.value = parsedVal;
      pendingLeg[`offset_${k}`] = `offset ${k} ${parsedVal}`;
    });
    try { localStorage.setItem("esp32_leg_offsets", JSON.stringify(legOffsets)); } catch (e) {}
  } else {
    targetLegs.forEach(k => {
      legAngles[k] = parsedVal;
      const valEl = document.getElementById(`${k}-val`);
      if (valEl) valEl.innerText = `${parsedVal} deg`;
      const sl = document.getElementById(`${k}-slider`);
      if (sl && sl.value != parsedVal) sl.value = parsedVal;
      pendingLeg[k] = `${k} ${parsedVal}`;
    });
    try { localStorage.setItem("esp32_leg_angles", JSON.stringify(legAngles)); } catch (e) {}
  }

  if (!legThrottleTimer) {
    legThrottleTimer = setTimeout(async () => {
      legThrottleTimer = null;
      const cmds = Object.values(pendingLeg);
      pendingLeg = {};
      for (let i = 0; i < cmds.length; i++) {
        await sendCommand(cmds[i]);
        if (i < cmds.length - 1) {
          await new Promise(r => setTimeout(r, 20));
        }
      }
    }, 40);
  }
}

async function nudgeLegAngle(leg, delta) {
  const targetLegs = getSyncedLegs(leg);
  for (let i = 0; i < targetLegs.length; i++) {
    const k = targetLegs[i];
    let next = (legAngles[k] || 90) + delta;
    if (next < 0) next = 0; if (next > 180) next = 180;
    legAngles[k] = next;
    if (!isOffsetMode) {
      const sl = document.getElementById(`${k}-slider`); if (sl) sl.value = next;
      const valEl = document.getElementById(`${k}-val`); if (valEl) valEl.innerText = `${next} deg`;
    }
    await sendCommand(`${k} ${next}`);
    if (i < targetLegs.length - 1) {
      await new Promise(r => setTimeout(r, 20));
    }
  }
  try { localStorage.setItem("esp32_leg_angles", JSON.stringify(legAngles)); } catch (e) {}
}

async function adjustOffset(leg, delta) {
  const targetLegs = getSyncedLegs(leg);
  for (let i = 0; i < targetLegs.length; i++) {
    const k = targetLegs[i];
    legOffsets[k] = (legOffsets[k] || 0) + delta;
    if (legOffsets[k] < -45) legOffsets[k] = -45;
    if (legOffsets[k] > 45) legOffsets[k] = 45;
    const offEl = document.getElementById(`o${k}-val`); if (offEl) offEl.innerText = legOffsets[k];
    if (isOffsetMode) {
      const sl = document.getElementById(`${k}-slider`); if (sl) sl.value = legOffsets[k];
      const valEl = document.getElementById(`${k}-val`); if (valEl) valEl.innerText = `${legOffsets[k] > 0 ? '+' : ''}${legOffsets[k]}`;
    }
    await sendCommand(`offset ${k} ${legOffsets[k]}`);
    appendLog("Trim", `${k.toUpperCase()} Offset -> ${legOffsets[k]}`);
    if (i < targetLegs.length - 1) {
      await new Promise(r => setTimeout(r, 20));
    }
  }
  try { localStorage.setItem("esp32_leg_offsets", JSON.stringify(legOffsets)); } catch (e) {}
}

async function saveCalibrationOffsets() {
  const btn = document.getElementById("save-cal-btn");
  const txt = document.getElementById("save-cal-text");
  if (txt) txt.innerText = "Saving to Flash...";
  if (btn) btn.style.background = "#0284c7";

  ["hl","hr","ll","lr"].forEach(k => { legAngles[k] = 90; });
  try {
    localStorage.setItem("esp32_leg_offsets", JSON.stringify(legOffsets));
    localStorage.setItem("esp32_leg_angles", JSON.stringify(legAngles));
  } catch (e) {}

  const cmd = `cal_set offsets ${legOffsets.ll} ${legOffsets.hr} ${legOffsets.hl} ${legOffsets.lr}`;
  await sendCommand(cmd);
  await new Promise(r => setTimeout(r, 40));
  await sendCommand("stand");

  if (isOffsetMode) toggleOffsetMode();
  else updateAllLegUI();

  appendLog("Calibration", `Offsets Saved: LL=${legOffsets.ll}, HR=${legOffsets.hr}, HL=${legOffsets.hl}, LR=${legOffsets.lr}`);
  showToast("Offsets saved to Flash (NVS)! Reset to 90 deg Stand", "success");

  if (txt) txt.innerText = "Saved to Flash (NVS)";
  if (btn) btn.style.background = "#059669";
  setTimeout(() => {
    if (txt) txt.innerText = "Save Calibration to Flash (NVS)";
    if (btn) btn.style.background = "#10b981";
  }, 1600);
}

function resetToStandPose() {
  ["hl","hr","ll","lr"].forEach(k => { legAngles[k] = 90; });
  try { localStorage.setItem("esp32_leg_angles", JSON.stringify(legAngles)); } catch (e) {}
  if (isOffsetMode) {
    isOffsetMode = false;
    const btn = document.getElementById("offset-mode-btn");
    if (btn) { btn.innerText = "Offset Mode: OFF"; btn.style.background = "#1e293b"; btn.style.color = "#f59e0b"; }
  }
  updateAllLegUI();
  sendCommand("stand");
  showToast("Aligned to 90 deg Stand Pose", "info");
  appendLog("Action", "Set joints to 90 deg Stand Pose");
}

function requestCalibrationSync() {
  sendCommand("cal_get");
  sendCommand("status");
  sendCommand("gethw");
  showToast("Syncing offsets and hardware from ESP32...", "info");
  appendLog("Sync", "Requested calibration offsets and hardware config.");
}

function toggleSyncFront() {
  syncFrontActive = !syncFrontActive;
  if (syncFrontActive && syncAllActive) syncAllActive = false;
  sendCommand(syncFrontActive ? "sync_front on" : "sync_front off");
  updateSyncUI();
  appendLog("Sync", `Front Legs Sync: ${syncFrontActive ? "ENABLED" : "DISABLED"}`);
}

function toggleSyncBack() {
  syncBackActive = !syncBackActive;
  if (syncBackActive && syncAllActive) syncAllActive = false;
  sendCommand(syncBackActive ? "sync_back on" : "sync_back off");
  updateSyncUI();
  appendLog("Sync", `Back Legs Sync: ${syncBackActive ? "ENABLED" : "DISABLED"}`);
}

function toggleSyncAll() {
  syncAllActive = !syncAllActive;
  syncFrontActive = syncAllActive;
  syncBackActive = syncAllActive;
  sendCommand(syncAllActive ? "sync_all on" : "sync_all off");
  updateSyncUI();
  appendLog("Sync", `All 4 Legs Sync: ${syncAllActive ? "ENABLED" : "DISABLED"}`);
}

function updateSyncUI() {
  const btnF = document.getElementById("sync-front-btn");
  const btnB = document.getElementById("sync-back-btn");
  const btnA = document.getElementById("sync-all-btn");
  const ind = document.getElementById("sync-status-indicator");

  if (syncFrontActive) btnF.classList.add("active"); else btnF.classList.remove("active");
  if (syncBackActive) btnB.classList.add("active"); else btnB.classList.remove("active");
  if (syncAllActive) btnA.classList.add("active"); else btnA.classList.remove("active");

  if (syncAllActive) { ind.innerText = "All 4 Synced"; ind.style.color = "#38bdf8"; }
  else if (syncFrontActive && syncBackActive) { ind.innerText = "Front & Back Synced"; ind.style.color = "#38bdf8"; }
  else if (syncFrontActive) { ind.innerText = "Front Synced"; ind.style.color = "#a855f7"; }
  else if (syncBackActive) { ind.innerText = "Back Synced"; ind.style.color = "#a855f7"; }
  else { ind.innerText = "Independent"; ind.style.color = "rgba(255,255,255,0.5)"; }
}

function applyReceivedAngles(hl, hr, ll, lr) {
  legAngles.hl = hl; legAngles.hr = hr; legAngles.ll = ll; legAngles.lr = lr;
  ["hl", "hr", "ll", "lr"].forEach(k => {
    if (document.activeElement !== document.getElementById(`${k}-slider`)) {
      const sl = document.getElementById(`${k}-slider`); if (sl) sl.value = legAngles[k];
      const lbl = document.getElementById(`${k}-val`); if (lbl) lbl.innerText = `${legAngles[k]} deg`;
    }
  });
}

function applyReceivedOffsets(text) {
  const moHL = text.match(/ohl=(-?\d+)/);
  const moHR = text.match(/ohr=(-?\d+)/);
  const moLL = text.match(/oll=(-?\d+)/);
  const moLR = text.match(/olr=(-?\d+)/);
  if (moHL) { legOffsets.hl = parseInt(moHL[1], 10); const el = document.getElementById("ohl-val"); if (el) el.innerText = moHL[1]; }
  if (moHR) { legOffsets.hr = parseInt(moHR[1], 10); const el = document.getElementById("ohr-val"); if (el) el.innerText = moHR[1]; }
  if (moLL) { legOffsets.ll = parseInt(moLL[1], 10); const el = document.getElementById("oll-val"); if (el) el.innerText = moLL[1]; }
  if (moLR) { legOffsets.lr = parseInt(moLR[1], 10); const el = document.getElementById("olr-val"); if (el) el.innerText = moLR[1]; }
  if (isOffsetMode) updateAllLegUI();
}

function applyReceivedHardwarePins(text) {
  const srvM = text.match(/srv=(\d+),(\d+),(\d+),(\d+)/);
  if (srvM) {
    hardwarePins.srv_ll = parseInt(srvM[1], 10);
    hardwarePins.srv_hr = parseInt(srvM[2], 10);
    hardwarePins.srv_hl = parseInt(srvM[3], 10);
    hardwarePins.srv_lr = parseInt(srvM[4], 10);
  }
  const sensM = text.match(/sens=(\d+),(\d+)/);
  if (sensM) {
    hardwarePins.sens_trig = parseInt(sensM[1], 10);
    hardwarePins.sens_echo = parseInt(sensM[2], 10);
  }
  const audM = text.match(/aud=(\d+),(\d+),(\d+)/);
  if (audM) {
    hardwarePins.aud_bclk = parseInt(audM[1], 10);
    hardwarePins.aud_lrc  = parseInt(audM[2], 10);
    hardwarePins.aud_din  = parseInt(audM[3], 10);
  }
  try { localStorage.setItem("esp32_hw_pins", JSON.stringify(hardwarePins)); } catch (e) {}
  populateHardwareModalInputs();
}

function applyReceivedHardwareNames(text) {
  const mHl = text.match(/hl=([^;]+)/);
  const mHr = text.match(/hr=([^;]+)/);
  const mLl = text.match(/ll=([^;]+)/);
  const mLr = text.match(/lr=([^;\n\r]+)/);
  if (mHl && mHl[1].trim()) legNames.hl = mHl[1].trim();
  if (mHr && mHr[1].trim()) legNames.hr = mHr[1].trim();
  if (mLl && mLl[1].trim()) legNames.ll = mLl[1].trim();
  if (mLr && mLr[1].trim()) legNames.lr = mLr[1].trim();
  try { localStorage.setItem("esp32_leg_names", JSON.stringify(legNames)); } catch (e) {}
  updateAllLegUI();
  populateHardwareModalInputs();
}

function openHardwareModal() {
  populateHardwareModalInputs();
  const m = document.getElementById("hw-config-modal");
  if (m) m.style.display = "flex";
}

function closeHardwareModal() {
  const m = document.getElementById("hw-config-modal");
  if (m) m.style.display = "none";
}

function populateHardwareModalInputs() {
  const setVal = (id, v) => { const el = document.getElementById(id); if (el && v !== undefined) el.value = v; };
  setVal("modal-name-hl", legNames.hl);
  setVal("modal-name-hr", legNames.hr);
  setVal("modal-name-ll", legNames.ll);
  setVal("modal-name-lr", legNames.lr);

  setVal("modal-pin-ll", hardwarePins.srv_ll);
  setVal("modal-pin-hr", hardwarePins.srv_hr);
  setVal("modal-pin-hl", hardwarePins.srv_hl);
  setVal("modal-pin-lr", hardwarePins.srv_lr);

  setVal("modal-pin-trig", hardwarePins.sens_trig);
  setVal("modal-pin-echo", hardwarePins.sens_echo);

  setVal("modal-pin-bclk", hardwarePins.aud_bclk);
  setVal("modal-pin-lrc",  hardwarePins.aud_lrc);
  setVal("modal-pin-din",  hardwarePins.aud_din);
}

async function saveAllHardwareConfig() {
  const getNum = (id, def) => { const v = parseInt(document.getElementById(id)?.value, 10); return isNaN(v) ? def : v; };
  const getStr = (id, def) => { const v = document.getElementById(id)?.value.trim(); return v || def; };

  legNames.hl = getStr("modal-name-hl", "Front Left (HL)");
  legNames.hr = getStr("modal-name-hr", "Front Right (HR)");
  legNames.ll = getStr("modal-name-ll", "Back Left (LL)");
  legNames.lr = getStr("modal-name-lr", "Back Right (LR)");

  hardwarePins.srv_ll = getNum("modal-pin-ll", 1);
  hardwarePins.srv_hr = getNum("modal-pin-hr", 2);
  hardwarePins.srv_hl = getNum("modal-pin-hl", 3);
  hardwarePins.srv_lr = getNum("modal-pin-lr", 4);

  hardwarePins.sens_trig = getNum("modal-pin-trig", 5);
  hardwarePins.sens_echo = getNum("modal-pin-echo", 6);

  hardwarePins.aud_bclk = getNum("modal-pin-bclk", 10);
  hardwarePins.aud_lrc  = getNum("modal-pin-lrc", 11);
  hardwarePins.aud_din  = getNum("modal-pin-din", 12);

  try {
    localStorage.setItem("esp32_leg_names", JSON.stringify(legNames));
    localStorage.setItem("esp32_hw_pins", JSON.stringify(hardwarePins));
  } catch (e) {}

  updateAllLegUI();

  // Transmit hardware reconfiguration to ESP32 over BLE with 20ms safety spacing
  await sendCommand(`setnames ${legNames.hl.replace(/\s+/g, "_")} ${legNames.hr.replace(/\s+/g, "_")} ${legNames.ll.replace(/\s+/g, "_")} ${legNames.lr.replace(/\s+/g, "_")}`);
  await new Promise(r => setTimeout(r, 20));
  await sendCommand(`setpins leg ${hardwarePins.srv_ll} ${hardwarePins.srv_hr} ${hardwarePins.srv_hl} ${hardwarePins.srv_lr}`);
  await new Promise(r => setTimeout(r, 20));
  await sendCommand(`setpins sens ${hardwarePins.sens_trig} ${hardwarePins.sens_echo}`);
  await new Promise(r => setTimeout(r, 20));
  await sendCommand(`setpins aud ${hardwarePins.aud_bclk} ${hardwarePins.aud_lrc} ${hardwarePins.aud_din}`);

  showToast("Hardware Pins & Names Saved!", "success");
  appendLog("Config", `GPIOs updated: Legs(${hardwarePins.srv_ll},${hardwarePins.srv_hr},${hardwarePins.srv_hl},${hardwarePins.srv_lr}) Sens(${hardwarePins.sens_trig},${hardwarePins.sens_echo}) Aud(${hardwarePins.aud_bclk},${hardwarePins.aud_lrc},${hardwarePins.aud_din})`);
  closeHardwareModal();
}

async function resetHardwareDefaults() {
  legNames = {
    hl: "Front Left (HL)",
    hr: "Front Right (HR)",
    ll: "Back Left (LL)",
    lr: "Back Right (LR)"
  };
  hardwarePins = {
    srv_ll: 1, srv_hr: 2, srv_hl: 3, srv_lr: 4,
    sens_trig: 5, sens_echo: 6,
    aud_bclk: 10, aud_lrc: 11, aud_din: 12
  };
  try {
    localStorage.setItem("esp32_leg_names", JSON.stringify(legNames));
    localStorage.setItem("esp32_hw_pins", JSON.stringify(hardwarePins));
  } catch (e) {}
  populateHardwareModalInputs();
  updateAllLegUI();
  await sendCommand("setpins leg 1 2 3 4");
  await new Promise(r => setTimeout(r, 20));
  await sendCommand("setpins sens 5 6");
  await new Promise(r => setTimeout(r, 20));
  await sendCommand("setpins aud 10 11 12");
  showToast("Restored GPIO Pin Defaults", "info");
}
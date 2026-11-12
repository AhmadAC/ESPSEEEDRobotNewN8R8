let legAngles = { hl: 90, hr: 90, ll: 90, lr: 90 };
let legOffsets = { hl: 0, hr: 0, ll: 0, lr: 11 };

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
  } catch (e) {}
  updateAllLegUI();
}

function updateAllLegUI() {
  ["hl", "hr", "ll", "lr"].forEach(k => {
    const offEl = document.getElementById(`o${k}-val`);
    if (offEl) offEl.innerText = legOffsets[k];

    const sl = document.getElementById(`${k}-slider`);
    const hdrEl = document.getElementById(`${k}-header`);
    const legNames = { hl: "Front Left (HL)", hr: "Front Right (HR)", ll: "Back Left (LL)", lr: "Back Right (LR)" };

    if (isOffsetMode) {
      if (sl) { sl.classList.add("offset-slider"); sl.min = "-45"; sl.max = "45"; sl.value = legOffsets[k]; }
      if (hdrEl) hdrEl.innerHTML = `${legNames[k]} Trim: <span id="${k}-val" style="color:#f59e0b; font-family:monospace; font-weight:700;">${legOffsets[k] > 0 ? '+' : ''}${legOffsets[k]}</span>`;
    } else {
      if (sl) { sl.classList.remove("offset-slider"); sl.min = "0"; sl.max = "180"; sl.value = legAngles[k]; }
      if (hdrEl) hdrEl.innerHTML = `${legNames[k]}: <span id="${k}-val" style="color:#38bdf8; font-family:monospace; font-weight:700;">${legAngles[k]} deg</span>`;
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
    legThrottleTimer = setTimeout(() => {
      legThrottleTimer = null;
      for (const k in pendingLeg) sendCommand(pendingLeg[k]);
      pendingLeg = {};
    }, 40);
  }
}

function nudgeLegAngle(leg, delta) {
  const targetLegs = getSyncedLegs(leg);
  targetLegs.forEach(k => {
    let next = (legAngles[k] || 90) + delta;
    if (next < 0) next = 0; if (next > 180) next = 180;
    legAngles[k] = next;
    if (!isOffsetMode) {
      const sl = document.getElementById(`${k}-slider`); if (sl) sl.value = next;
      const valEl = document.getElementById(`${k}-val`); if (valEl) valEl.innerText = `${next} deg`;
    }
    sendCommand(`${k} ${next}`);
  });
  try { localStorage.setItem("esp32_leg_angles", JSON.stringify(legAngles)); } catch (e) {}
}

function adjustOffset(leg, delta) {
  const targetLegs = getSyncedLegs(leg);
  targetLegs.forEach(k => {
    legOffsets[k] = (legOffsets[k] || 0) + delta;
    if (legOffsets[k] < -45) legOffsets[k] = -45;
    if (legOffsets[k] > 45) legOffsets[k] = 45;
    const offEl = document.getElementById(`o${k}-val`); if (offEl) offEl.innerText = legOffsets[k];
    if (isOffsetMode) {
      const sl = document.getElementById(`${k}-slider`); if (sl) sl.value = legOffsets[k];
      const valEl = document.getElementById(`${k}-val`); if (valEl) valEl.innerText = `${legOffsets[k] > 0 ? '+' : ''}${legOffsets[k]}`;
    }
    sendCommand(`offset ${k} ${legOffsets[k]}`);
    appendLog("Trim", `${k.toUpperCase()} Offset -> ${legOffsets[k]}`);
  });
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
  await sendCommand("stand");

  if (isOffsetMode) toggleOffsetMode();
  else updateAllLegUI();

  appendLog("Calibration", `Offsets Saved: LL=${legOffsets.ll}, HR=${legOffsets.hr}, HL=${legOffsets.hl}, LR=${legOffsets.lr}`);
  showToast(`Offsets saved to Flash (NVS)! Reset to 90° Stand`, "success");

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
  showToast("Syncing offsets from ESP32...", "info");
  appendLog("Sync", "Requested calibration offsets and status.");
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
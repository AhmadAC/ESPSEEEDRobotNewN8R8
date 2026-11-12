let sensorEnabled = false;
let isObstacleTripped = false;
let telemetryPollTimer = null;

function initSensorStorage() {
  // Sensor is strictly disabled by default
  sensorEnabled = false;
  updateSensorState(false);

  try {
    const th = localStorage.getItem("esp32_sens_thresh");
    if (th) {
      const sl = document.getElementById("sens-dist-slider");
      const valLbl = document.getElementById("sens-dist-val");
      if (sl) sl.value = th;
      if (valLbl) valLbl.innerText = th + "cm";
    }
    const tripAct = localStorage.getItem("esp32_sens_trip_act");
    if (tripAct) {
      const el = document.getElementById("sens-trip-act");
      if (el) el.value = tripAct;
    }
    const tripAud = localStorage.getItem("esp32_sens_trip_aud");
    if (tripAud) {
      const el = document.getElementById("sens-trip-aud");
      if (el) el.value = tripAud;
    }
    const clearAct = localStorage.getItem("esp32_sens_clear_act");
    if (clearAct) {
      const el = document.getElementById("sens-clear-act");
      if (el) el.value = clearAct;
    }
    const clearAud = localStorage.getItem("esp32_sens_clear_aud");
    if (clearAud) {
      const el = document.getElementById("sens-clear-aud");
      if (el) el.value = clearAud;
    }
  } catch (e) {}
}

function updateSensorState(enabled) {
  sensorEnabled = enabled;
  const btn = document.getElementById("sensor-toggle-btn");
  if (btn) {
    btn.innerText = sensorEnabled ? "ENABLED" : "DISABLED";
    btn.style.background = sensorEnabled ? "#10b981" : "#ef4444";
  }

  if (!sensorEnabled) {
    const distEl = document.getElementById("sens-live-dist");
    const tagEl = document.getElementById("sens-source-tag");
    if (distEl) {
      distEl.innerText = "--";
      distEl.style.color = "rgba(255,255,255,0.3)";
    }
    if (tagEl) {
      tagEl.innerText = "OFF";
      tagEl.style.color = "rgba(255,255,255,0.4)";
    }
    isObstacleTripped = false;
  }
}

function updateDistanceUI(distVal, thVal, sourceLabel = "BLE") {
  const distEl = document.getElementById("sens-live-dist");
  const tagEl = document.getElementById("sens-source-tag");

  // If sensor is disabled, display standby indicators and suppress trip logic
  if (!sensorEnabled) {
    if (distEl) {
      distEl.innerText = "--";
      distEl.style.color = "rgba(255,255,255,0.3)";
    }
    if (tagEl) {
      tagEl.innerText = "OFF";
      tagEl.style.color = "rgba(255,255,255,0.4)";
    }
    isObstacleTripped = false;
    return;
  }

  // Active state: update source and live distance
  if (tagEl) {
    tagEl.innerText = sourceLabel;
    tagEl.style.color = sourceLabel === "BLE" ? "#38bdf8" : "#10b981";
  }

  const numDist = parseFloat(distVal);
  const numTh = parseFloat(thVal) || 20.0;

  if (!isNaN(numDist) && numDist > 0.0 && numDist <= 350.0) {
    if (distEl) {
      distEl.innerText = numDist.toFixed(1) + "cm";
      distEl.style.color = (numDist < numTh) ? "#ef4444" : "#10b981";
    }

    if (numDist < numTh) {
      if (!isObstacleTripped) {
        isObstacleTripped = true;
        triggerObstacleTrip(numDist, numTh);
      }
    } else {
      if (isObstacleTripped) {
        isObstacleTripped = false;
        triggerObstacleClear(numDist, numTh);
      }
    }
  } else {
    if (distEl) {
      distEl.innerText = "--";
      distEl.style.color = "rgba(255,255,255,0.4)";
    }
  }
}

function triggerObstacleTrip(dist, th) {
  const tripAct = document.getElementById("sens-trip-act")?.value || "sit";
  const tripAud = document.getElementById("sens-trip-aud")?.value || "beep";

  appendLog("Obstacle", `TRIP TRIGGERED: ${dist.toFixed(1)}cm < ${th}cm (Action: ${tripAct}, Audio: ${tripAud})`);

  if (tripAct && tripAct !== "none") {
    sendCommand(tripAct);
  }

  playSensorSound(tripAud);
}

function triggerObstacleClear(dist, th) {
  const clearAct = document.getElementById("sens-clear-act")?.value || "stand";
  const clearAud = document.getElementById("sens-clear-aud")?.value || "none";

  appendLog("Obstacle", `CLEARED: ${dist.toFixed(1)}cm >= ${th}cm (Action: ${clearAct}, Audio: ${clearAud})`);

  if (clearAct && clearAct !== "none") {
    sendCommand(clearAct);
  }

  playSensorSound(clearAud);
}

function playSensorSound(audType) {
  if (!audType || audType === "none") return;

  if (audType === "dog_bark" || audType === "bark") {
    sendCommand("bark");
  } else if (audType === "test_beep" || audType === "beep") {
    sendCommand("beep");
  } else {
    sendCommand(audType);
  }
}

function toggleSensor() {
  sensorEnabled = !sensorEnabled;
  updateSensorState(sensorEnabled);
  try {
    localStorage.setItem("esp32_sensor_enabled", sensorEnabled ? "true" : "false");
  } catch (e) {}

  if (sensorEnabled) {
    sendCommand("sensor on");
    showToast("Obstacle Sensor ENABLED", "success");
    appendLog("Sensor", "Command: sensor on (ARMED)");
  } else {
    sendCommand("sensor off");
    updateDistanceUI(null, null);
    showToast("Obstacle Sensor DISABLED", "warn");
    appendLog("Sensor", "Command: sensor off (DISARMED)");
  }
}

function updateSensorConfig() {
  const th = parseInt(document.getElementById("sens-dist-slider")?.value || 20, 10);
  const trip = document.getElementById("sens-trip-act")?.value || "sit";
  const clear = document.getElementById("sens-clear-act")?.value || "stand";
  
  const tripAudEl = document.getElementById("sens-trip-aud");
  const clearAudEl = document.getElementById("sens-clear-aud");
  const atrip = tripAudEl ? tripAudEl.value : "beep";
  const aclear = clearAudEl ? clearAudEl.value : "none";

  try {
    localStorage.setItem("esp32_sens_thresh", th);
    localStorage.setItem("esp32_sens_trip_act", trip);
    localStorage.setItem("esp32_sens_trip_aud", atrip);
    localStorage.setItem("esp32_sens_clear_act", clear);
    localStorage.setItem("esp32_sens_clear_aud", aclear);
  } catch (e) {}

  sendCommand(`sens_thresh ${th}`);
  sendCommand(`sens_actions ${trip} ${clear}`);
  sendCommand(`sens_audio ${atrip} ${aclear}`);
  appendLog("Config", `Sensor set -> Thresh: ${th}cm, Trip: ${trip}/${atrip}, Clear: ${clear}/${aclear}`);
}

function syncSensorToRobot() {
  const th = parseInt(document.getElementById("sens-dist-slider")?.value || 20, 10);
  const trip = document.getElementById("sens-trip-act")?.value || "sit";
  const clear = document.getElementById("sens-clear-act")?.value || "stand";
  const tripAud = document.getElementById("sens-trip-aud")?.value || "beep";
  const clearAud = document.getElementById("sens-clear-aud")?.value || "none";

  sendCommand(`sens_thresh ${th}`);
  sendCommand(`sens_actions ${trip} ${clear}`);
  sendCommand(`sens_audio ${tripAud} ${clearAud}`);

  if (sensorEnabled) {
    sendCommand("sensor on");
  } else {
    sendCommand("sensor off");
  }
}

function startActivePolling() {
  if (telemetryPollTimer) clearInterval(telemetryPollTimer);
  telemetryPollTimer = setInterval(async () => {
    if (!isConnected) return;
    
    // Query status without flooding the serial bus (1500ms cycle)
    await sendCommand("status");

    const backend = getBleBackend();
    if (backend?.type === "capacitor" && nativeDeviceId) {
      try {
        const Ble = backend.ble;
        const res = await Ble.read({
          deviceId: nativeDeviceId,
          service: UART_SERVICE_UUID,
          characteristic: UART_TX_CHAR_UUID
        });
        if (res) {
          const text = universalBleDecode(res.value !== undefined ? res.value : res);
          if (text) handleIncomingBleData(text, "READ");
        }
      } catch (e) {}
    }
  }, 1500);
}

function stopTelemetryTimers() {
  if (telemetryPollTimer) {
    clearInterval(telemetryPollTimer);
    telemetryPollTimer = null;
  }
}
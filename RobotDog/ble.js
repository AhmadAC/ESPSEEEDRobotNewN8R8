const UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const UART_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const UART_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

let isConnected = false;
let bleBackend = null;
let nativeDeviceId = null;
let webDevice = null;
let webServer = null;
let webTxChar = null;

let rxCount = 0;
let txCount = 0;
let rawLogLines = [];
let cmdQueue = [];
let isProcessingQueue = false;
let toastTimeout = null;

function updateCounterDisplay() {
  const el = document.getElementById("ble-counters");
  if (el) el.innerText = `RX: ${rxCount} | TX: ${txCount}`;
}

function showToast(msg, type = "info") {
  const toast = document.getElementById("toast-notification");
  const text = document.getElementById("toast-message");
  const icon = document.getElementById("toast-icon");
  if (!toast || !text) return;

  text.innerText = msg;
  if (type === "success") {
    toast.style.borderColor = "#10b981";
    if (icon) icon.style.color = "#10b981";
  } else if (type === "error") {
    toast.style.borderColor = "#ef4444";
    if (icon) icon.style.color = "#ef4444";
  } else if (type === "warn") {
    toast.style.borderColor = "#f59e0b";
    if (icon) icon.style.color = "#f59e0b";
  } else {
    toast.style.borderColor = "#38bdf8";
    if (icon) icon.style.color = "#38bdf8";
  }

  toast.classList.add("show");
  if (toastTimeout) clearTimeout(toastTimeout);
  toastTimeout = setTimeout(() => { toast.classList.remove("show"); }, 2400);
}

function getBleBackend() {
  if (window.BluetoothLe) return { type: "capacitor", ble: window.BluetoothLe };
  if (window.parent?.BluetoothLe) return { type: "capacitor", ble: window.parent.BluetoothLe };
  if (window.Capacitor?.Plugins?.BluetoothLe) return { type: "capacitor", ble: window.Capacitor.Plugins.BluetoothLe };
  if (window.parent?.Capacitor?.Plugins?.BluetoothLe) return { type: "capacitor", ble: window.parent.Capacitor.Plugins.BluetoothLe };
  if (navigator.bluetooth) return { type: "web-bluetooth" };
  return null;
}

function initBackendUI() {
  const backend = getBleBackend();
  const badge = document.getElementById("backend-badge");
  if (!backend) { badge.innerText = "No BLE"; return; }
  if (backend.type === "capacitor") {
    bleBackend = "capacitor";
    badge.innerText = "iOS CoreBLE";
    badge.style.color = "#6ee7b7";
  } else {
    bleBackend = "web-bluetooth";
    badge.innerText = "Web BLE";
    badge.style.color = "#7dd3fc";
  }
}

async function toggleConnection() {
  if (isConnected) await disconnectDevice();
  else await connectDevice();
}

async function connectDevice() {
  const backend = getBleBackend();
  if (!backend) {
    showToast("Bluetooth is unavailable in this environment.", "error");
    appendLog("Error", "No BluetoothLe plugin found in window or parent.");
    return;
  }

  setUiState("Connecting...", "#f59e0b", "Connecting");
  appendLog("BLE", "Opening BLE device picker...");

  try {
    if (backend.type === "capacitor") {
      const Ble = backend.ble;
      await Ble.initialize();

      const device = await Ble.requestDevice({ services: [UART_SERVICE_UUID] });
      if (!device || !device.deviceId) throw new Error("No device selected.");

      nativeDeviceId = device.deviceId;
      appendLog("BLE", `Connecting to ${device.name || nativeDeviceId}...`);

      try { if (Ble.stopLEScan) await Ble.stopLEScan(); } catch (e) {}

      await Ble.connect({ deviceId: nativeDeviceId });
      await new Promise(r => setTimeout(r, 600));

      if (Ble.addListener) {
        try {
          await Ble.addListener("notification", (result) => {
            if (result) {
              const decoded = universalBleDecode(result.value !== undefined ? result.value : result);
              if (decoded) handleIncomingBleData(decoded, "NOTIF");
            }
          });
        } catch (e) {}
      }

      try {
        await Ble.startNotifications({
          deviceId: nativeDeviceId,
          service: UART_SERVICE_UUID,
          characteristic: UART_TX_CHAR_UUID
        }, (result) => {
          if (result) {
            const decoded = universalBleDecode(result.value !== undefined ? result.value : result);
            if (decoded) handleIncomingBleData(decoded, "NOTIF");
          }
        });
        appendLog("BLE", "Notifications registered on TX Characteristic.");
      } catch (notifErr) {
        appendLog("BLE Warn", `Notify registration: ${notifErr.message || notifErr}`);
      }

      onConnectedSuccess(device.name || "ESP32-Robot");

    } else if (backend.type === "web-bluetooth") {
      webDevice = await navigator.bluetooth.requestDevice({
        filters: [
          { services: [UART_SERVICE_UUID] },
          { namePrefix: "ESP" }
        ],
        optionalServices: [UART_SERVICE_UUID]
      });

      webDevice.addEventListener("gattserverdisconnected", onDisconnectedCleanup);
      webServer = await webDevice.gatt.connect();

      const service = await webServer.getPrimaryService(UART_SERVICE_UUID);
      webTxChar = await service.getCharacteristic(UART_RX_CHAR_UUID);

      try {
        const rxNotifyChar = await service.getCharacteristic(UART_TX_CHAR_UUID);
        await rxNotifyChar.startNotifications();
        rxNotifyChar.addEventListener("characteristicvaluechanged", (e) => {
          const decoded = universalBleDecode(e.target.value);
          if (decoded) handleIncomingBleData(decoded, "WEB-BLE");
        });
      } catch (notifErr) {}

      onConnectedSuccess(webDevice.name || "ESP32-Robot");
    }
  } catch (err) {
    appendLog("Error", `Connection failed: ${err.message || err}`);
    showToast("Bluetooth failed to connect", "error");
    onDisconnectedCleanup();
  }
}

async function disconnectDevice() {
  const backend = getBleBackend();
  try {
    if (backend && backend.type === "capacitor" && nativeDeviceId) {
      await backend.ble.disconnect({ deviceId: nativeDeviceId });
    } else if (webDevice?.gatt?.connected) {
      await webDevice.gatt.disconnect();
    }
  } catch (e) {}
  onDisconnectedCleanup();
}

function onConnectedSuccess(name) {
  isConnected = true;
  document.getElementById("device-name").innerText = name;
  setUiState("Connected", "#10b981", "Disconnect");
  appendLog("BLE", `Connected to ${name}. Telemetry active.`);
  showToast(`Connected to ${name}`, "success");

  setTimeout(() => {
    sendCommand("status");
    setTimeout(() => sendCommand("cal_get"), 250);
    setTimeout(() => sendCommand("getswitchkeys"), 500);
    setTimeout(() => {
      if (typeof syncSensorToRobot === "function") {
        syncSensorToRobot();
      }
    }, 750);
  }, 500);

  startActivePolling();
}

function onDisconnectedCleanup() {
  isConnected = false;
  nativeDeviceId = null;
  webDevice = null;
  webServer = null;
  webTxChar = null;
  document.getElementById("device-name").innerText = "None Connected";
  setUiState("Disconnected", "#f43f5e", "Connect BLE");
  appendLog("BLE", "Device disconnected.");

  stopTelemetryTimers();
}

async function sendCommand(commandText) {
  cmdQueue.push(commandText);
  if (isProcessingQueue) return;
  isProcessingQueue = true;

  while (cmdQueue.length > 0) {
    const nextCmd = cmdQueue.shift();
    await executeSendCommand(nextCmd);
  }
  isProcessingQueue = false;
}

function textToHexBytes(str) {
  let hex = "";
  for (let i = 0; i < str.length; i++) {
    let h = str.charCodeAt(i).toString(16);
    hex += (h.length === 1 ? "0" + h : h) + " ";
  }
  return hex.trim();
}

function textToBase64(str) {
  try { return btoa(str); } catch (e) { return ""; }
}

async function executeSendCommand(commandText) {
  if (!isConnected) {
    appendLog("Offline", commandText);
    return;
  }

  const backend = getBleBackend();
  const payload = commandText.endsWith("\n") ? commandText : (commandText + "\n");

  try {
    if (backend && backend.type === "capacitor" && nativeDeviceId) {
      const Ble = backend.ble;
      const hexPayload = textToHexBytes(payload);
      const b64Payload = textToBase64(payload);

      try {
        await Ble.writeWithoutResponse({
          deviceId: nativeDeviceId,
          service: UART_SERVICE_UUID,
          characteristic: UART_RX_CHAR_UUID,
          value: hexPayload
        });
      } catch (writeErr) {
        await Ble.write({
          deviceId: nativeDeviceId,
          service: UART_SERVICE_UUID,
          characteristic: UART_RX_CHAR_UUID,
          value: b64Payload
        });
      }

      txCount++;
      updateCounterDisplay();
      // Suppress logging of routine status commands to prevent console spam
      if (commandText !== "status") {
        appendLog("TX", commandText);
      }

    } else if (webTxChar) {
      const encoder = new TextEncoder();
      const bytes = encoder.encode(payload);

      if (webTxChar.writeValueWithoutResponse) await webTxChar.writeValueWithoutResponse(bytes);
      else await webTxChar.writeValue(bytes);

      txCount++;
      updateCounterDisplay();
      if (commandText !== "status") {
        appendLog("TX", commandText);
      }
    }
  } catch (err) {
    appendLog("Send Error", err.message || err);
  }
}

function universalBleDecode(val) {
  if (val === null || val === undefined) return "";

  if (typeof val === "object" && val !== null && val.value !== undefined) {
    val = val.value;
  }

  if (typeof val === "object" && val !== null) {
    try {
      const buf = val.buffer ? val.buffer : val;
      if (buf instanceof ArrayBuffer || ArrayBuffer.isView(val)) {
        return new TextDecoder().decode(val);
      }
    } catch (e) {}
  }

  if (typeof val === "string") {
    let raw = val.trim();
    if (!raw) return "";

    const cleanHex = raw.replace(/[\s:]+/g, "");
    if (/^[0-9a-fA-F]+$/.test(cleanHex) && cleanHex.length >= 2 && cleanHex.length % 2 === 0) {
      let hexDecoded = "";
      for (let i = 0; i < cleanHex.length; i += 2) {
        const charCode = parseInt(cleanHex.substring(i, i + 2), 16);
        if (charCode !== 0) {
          hexDecoded += String.fromCharCode(charCode);
        }
      }
      if (hexDecoded && (/^[\x20-\x7E\r\n\t]+$/.test(hexDecoded) || hexDecoded.includes("SENS:") || hexDecoded.includes("STATUS:") || hexDecoded.includes("ANG:") || hexDecoded.includes("CAL:") || hexDecoded.includes(">>"))) {
        return hexDecoded;
      }
    }

    try {
      const b64Decoded = atob(raw);
      if (b64Decoded && (/^[\x20-\x7E\r\n\t]+$/.test(b64Decoded) || b64Decoded.includes("SENS:") || b64Decoded.includes("STATUS:") || b64Decoded.includes("ANG:") || b64Decoded.includes("CAL:"))) {
        return b64Decoded;
      }
    } catch (e) {}

    return raw;
  }

  return String(val);
}

function handleIncomingBleData(rawText, sourceTag = "BLE") {
  if (!rawText) return;

  if (typeof rawText === "string") {
    const trimmed = rawText.trim();
    if (/^[0-9a-fA-F]{4,}$/.test(trimmed) && trimmed.length % 2 === 0) {
      const decodedHex = universalBleDecode(trimmed);
      if (decodedHex !== trimmed) {
        rawText = decodedHex;
      }
    }
  }

  rxCount++;
  updateCounterDisplay();

  const lines = rawText.split("\n");
  lines.forEach(line => {
    let text = line.trim();
    if (!text) return;

    const lineHexClean = text.replace(/[\s:]+/g, "");
    if (/^[0-9a-fA-F]{4,}$/.test(lineHexClean) && lineHexClean.length % 2 === 0) {
      let candidate = "";
      for (let i = 0; i < lineHexClean.length; i += 2) {
        const code = parseInt(lineHexClean.substring(i, i + 2), 16);
        if (code !== 0) candidate += String.fromCharCode(code);
      }
      if (candidate.includes("SENS:") || candidate.includes("STATUS:") || candidate.includes("ANG:") || candidate.includes("CAL:") || candidate.includes(">>")) {
        text = candidate.trim();
      }
    }

    // Filter continuous telemetry packets from flooding the console
    if (!text.startsWith("SENS:") && !text.startsWith("STATUS:") && !text.startsWith("ANG:")) {
      appendLog(sourceTag, text);
    }

    if (text.startsWith("SENS:")) {
      const parts = text.substring(5).split(",");
      if (parts.length >= 3) {
        const dist = parseFloat(parts[0]);
        const en = (parts[1] === "1");
        const th = parseInt(parts[2], 10);
        updateSensorState(en);
        updateDistanceUI(dist, th, sourceTag);
      }
      return;
    }

    if (text.startsWith("ANG:")) {
      const parts = text.substring(4).split(",");
      if (parts.length >= 4 && !isOffsetMode) {
        applyReceivedAngles(parseInt(parts[0], 10), parseInt(parts[1], 10), parseInt(parts[2], 10), parseInt(parts[3], 10));
      }
      return;
    }

    if (text.startsWith("STATUS:")) {
      const matchV = text.match(/vol=(\d+)/i);
      if (matchV) updateVolDisplay(matchV[1]);

      const mDist = text.match(/dist=([-\d.]+)/i);
      const mSen = text.match(/sen=(\d+)/i);
      const mTh = text.match(/th=(\d+)/i);

      if (mDist) {
        const distVal = parseFloat(mDist[1]);
        const thVal = mTh ? parseInt(mTh[1], 10) : 20;
        updateDistanceUI(distVal, thVal, sourceTag);
      }
      if (mSen) {
        updateSensorState(mSen[1] === "1");
      }
    }
    else if (text.startsWith("CAL:")) {
      applyReceivedOffsets(text);
    }
  });
}

function setUiState(statusText, dotColor, btnText) {
  document.getElementById("status-text").innerText = statusText;
  const dot = document.getElementById("status-dot");
  dot.style.background = dotColor;
  dot.style.boxShadow = `0 0 6px ${dotColor}`;
  const btn = document.getElementById("connect-btn");
  btn.querySelector("span").innerText = btnText;
}

function appendLog(sender, text) {
  const term = document.getElementById("terminal");
  if (!term) return;
  const time = new Date().toLocaleTimeString().split(" ")[0];
  const entry = `[${time}] ${sender}: ${text}`;
  rawLogLines.push(entry);
  if (rawLogLines.length > 250) rawLogLines.shift();

  const div = document.createElement("div");
  let senderColor = "#38bdf8";
  if (sender === "ESP32" || sender === "NOTIF" || sender === "READ") senderColor = "#34d399";
  if (sender === "Switch") senderColor = "#c084fc";
  if (sender === "Sync") senderColor = "#f472b6";
  if (sender === "Calibration" || sender === "Trim") senderColor = "#f59e0b";
  if (sender.includes("Error") || sender.includes("Warn")) senderColor = "#f87171";
  if (sender === "TX") senderColor = "#fbbf24";
  if (sender === "Obstacle") senderColor = "#f43f5e";
  if (sender === "Sensor") senderColor = "#a855f7";

  div.innerHTML = `<span style="color:rgba(255,255,255,0.3);">[${time}]</span> <span style="color:${senderColor}; font-weight:700;">${sender}:</span> <span>${escapeHtml(text)}</span>`;
  term.appendChild(div);
  term.scrollTop = term.scrollHeight;
}

function clearLogs() {
  rawLogLines = [];
  rxCount = 0;
  txCount = 0;
  updateCounterDisplay();
  document.getElementById("terminal").innerHTML = '<div style="color:rgba(255,255,255,0.4); font-style:italic;">Logs cleared. Ready...</div>';
}

function escapeHtml(str) {
  return String(str).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function exportTerminalLogs() {
  const header = `ROBOT DIAGNOSTIC LOG\nExported: ${new Date().toISOString()}\nBackend: ${bleBackend || "None"}\nRX: ${rxCount} | TX: ${txCount}\n\n`;
  const fullText = header + rawLogLines.join("\n");
  const filename = `robot-log-${new Date().toISOString().replace(/[:.]/g, "-")}.txt`;

  if (navigator.share && navigator.canShare) {
    try {
      const file = new File([fullText], filename, { type: "text/plain" });
      if (navigator.canShare({ files: [file] })) {
        navigator.share({ files: [file], title: "Robot Log" }).catch(() => {});
        return;
      }
    } catch (e) {}
  }

  const blob = new Blob([fullText], { type: "text/plain;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
let currentVolumeNum = 80;
let pendingVol = null;
let volThrottleTimer = null;

function updateVolDisplay(val) {
  currentVolumeNum = parseInt(val, 10);
  const lbl = document.getElementById("vol-val");
  const sl = document.getElementById("vol-slider");
  if (lbl) lbl.innerText = `Vol: ${val}%`;
  if (sl && document.activeElement !== sl) sl.value = val;
}

function onVolSliderMove(val) {
  currentVolumeNum = parseInt(val, 10);
  const lbl = document.getElementById("vol-val");
  if (lbl) lbl.innerText = `Vol: ${val}%`;
  pendingVol = val;

  if (!volThrottleTimer) {
    volThrottleTimer = setTimeout(() => {
      volThrottleTimer = null;
      if (pendingVol !== null) {
        sendCommand(`vol ${pendingVol}`);
        pendingVol = null;
      }
    }, 50);
  }
}
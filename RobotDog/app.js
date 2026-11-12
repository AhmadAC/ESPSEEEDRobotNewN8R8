function handleSendCustom(e) {
  e.preventDefault();
  const input = document.getElementById("cmd-input");
  const val = input.value.trim();
  if (!val) return;
  sendCommand(val);
  input.value = "";
}

window.addEventListener("DOMContentLoaded", () => {
  initBackendUI();
  initLegStorage();
  initSwitchButtonMap();
  initSensorStorage();
  pollGamepadThrottled();
});
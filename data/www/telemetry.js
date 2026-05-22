(function () {
  function getSaved() {
    try {
      return localStorage.getItem('telemetry') === '1';
    } catch (e) {
      return false;
    }
  }
  function setSaved(v) {
    try {
      localStorage.setItem('telemetry', v ? '1' : '0');
    } catch (e) {}
  }
  function applyUI() {
    var el = document.getElementById('telemetry');
    if (!el) return;
    el.classList.remove('on');
    el.classList.remove('off');
    el.classList.add(getSaved() ? 'on' : 'off');
  }
  function sendToDevice() {
    var v = getSaved() ? 1 : 0;
    try {
      if (window.websocket && websocket.readyState === 1) {
        websocket.send('telemetry=' + v);
        return true;
      }
    } catch (e) {}
    return false;
  }
  document.addEventListener('DOMContentLoaded', function () {
    applyUI();
    var el = document.getElementById('telemetry');
    if (el) {
      el.addEventListener('click', function () {
        setTimeout(function () {
          var enabled = el.classList.contains('on');
          setSaved(enabled);
          sendToDevice();
        }, 0);
      });
    }
    var tries = 0;
    var t = setInterval(function () {
      tries++;
      if (sendToDevice() || tries >= 20) clearInterval(t);
    }, 250);
  });
})();

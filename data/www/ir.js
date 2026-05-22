var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var wserrcnt = 0;
var wstimeout;

window.addEventListener('load', onLoad);

function initWebSocket() {
  clearTimeout(wstimeout);
  websocket = new WebSocket(gateway);
  websocket.onopen = onOpen;
  websocket.onclose = onClose;
  websocket.onmessage = onMessage;
}

function onOpen(event) {}

function onClose(event) {
  location.href = "/";
}

function onMessage(event) {
  var data = JSON.parse(event.data);
  if (data.ircode) {
    document.getElementById('protocol').innerText = data.protocol;
    var elements = document.getElementsByClassName("irrecordvalue");
    for (var i = 0; i < elements.length; i++) {
      if (elements[i].classList.contains("active")) {
        elements[i].innerText = '0x' + data.ircode.toString(16).toUpperCase();
        break;
      }
    }
  }
  if (data.irvals) {
    var elements2 = document.getElementsByClassName("irrecordvalue");
    for (var j = 0; j < elements2.length; j++) {
      var val = data.irvals[j];
      if (val > 0) {
        elements2[j].innerText = '0x' + val.toString(16).toUpperCase();
      } else {
        elements2[j].innerText = "";
      }
    }
  }
}

function checkSelect() {
  var elements = document.getElementsByClassName("irradio");
  var chkid = 0;
  for (var i = 0; i < elements.length; i++) {
    elements[i].classList.remove("active");
    elements[i].parentElement.getElementsByClassName("irrecordvalue")[0].classList.remove("active");
    if (elements[i] === this) chkid = i;
  }
  var ts = this !== window ? this : elements[0];
  ts.classList.add("active");
  ts.parentElement.getElementsByClassName("irrecordvalue")[0].classList.add("active");
  if (this !== window) websocket.send('chkid=' + chkid);
  document.getElementById('protocol').innerText = "";
}

function irbuttonClick() {
  var elements = document.getElementsByClassName("irbutton");
  var hasactive = this.classList.contains("active");
  if (hasactive) {
    backRecord();
    return;
  }
  var btnid = -1;
  var slotAttr = this.getAttribute("data-slot");
  if (slotAttr !== null) {
    btnid = parseInt(slotAttr, 10);
  } else {
    for (var i = 0; i < elements.length; i++) {
      if (!hasactive && elements[i] == this) btnid = i;
    }
  }

  for (var j = 0; j < elements.length; j++) {
    elements[j].classList.remove("active");
  }

  document.getElementById("irrecordtitle").innerHTML = 'Nagraj przycisk <span>' + this.innerText + '</span>';
  document.getElementById("irrecord").classList.remove("hidden");
  document.getElementById("irstartrecord").classList.add("hidden");
  this.classList.add("active");
  var radios = document.getElementsByClassName("irradio");
  if (radios.length) {
    checkSelect.call(radios[0]);
  } else {
    checkSelect();
  }
  document.getElementById('protocol').innerText = "";
  websocket.send('irbtn=' + btnid);
}

function backRecord() {
  var elements = document.getElementsByClassName("irbutton");
  for (var i = 0; i < elements.length; i++) {
    elements[i].classList.remove("active");
  }
  document.getElementById("irrecord").classList.add("hidden");
  document.getElementById("irstartrecord").classList.remove("hidden");
  websocket.send('irbtn=-1');
}

function irClear(el) {
  el.parentElement.getElementsByClassName("irrecordvalue")[0].innerText = "";
  document.getElementById('protocol').innerText = "";
  websocket.send('irclr=' + el.parentElement.getElementsByClassName("irradio")[0].getAttribute('data-id'));
}

function initControls() {
  var elements = document.getElementsByClassName("irbutton");
  for (var i = 0; i < elements.length; i++) {
    elements[i].addEventListener('click', irbuttonClick, false);
  }

  elements = document.getElementsByClassName("irradio");
  for (var j = 0; j < elements.length; j++) {
    elements[j].addEventListener('click', checkSelect, false);
  }

  var done = document.getElementById("done_ir");
  if (done) {
    done.addEventListener('click', function (e) {
      if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send('irbtn=-1');
      }
    }, false);
  }
}

function onLoad(event) {
  initWebSocket();
  initControls();
}

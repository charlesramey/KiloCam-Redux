const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>KiloCam Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f4f4f4; }
    h1 { color: #333; }
    .card { background: white; padding: 20px; margin: 10px auto; max-width: 600px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    button { background-color: #008CBA; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin: 5px; }
    button.red { background-color: #f44336; }
    button.green { background-color: #4CAF50; }
    input[type=text], input[type=number] { width: 100%; padding: 10px; margin: 8px 0; display: inline-block; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
    img.preview { max-width: 100%; height: auto; margin-top: 10px; border-radius: 4px; }
    .status-bar { padding: 10px; background: #e7f3fe; border-left: 6px solid #2196F3; margin-bottom: 15px; text-align: left; }
  </style>
</head>
<body>
  <h1>KiloCam Control</h1>

  <div class="card">
    <h2>Status</h2>
    <div class="status-bar" id="status">Loading...</div>
    <p>Device Time: <span id="devTime">--:--:--</span></p>
    <button onclick="syncTime()">Sync Browser Time</button>
  </div>

  <div class="card">
    <h2>Actions</h2>
    <button class="green" onclick="startCollection()">Start Collection Loop</button>
    <button class="red" onclick="shutdown()">Shutdown (Sleep)</button>
    <br><br>
    <button onclick="toggleLight()">Toggle Light</button>
    <button onclick="takePhoto()">Test Photo</button>
    <div id="preview-container"></div>
  </div>

  <div class="card">
    <h2>Settings</h2>
    <form onsubmit="event.preventDefault(); saveSettings();">
      <label for="devName">Device Name (SSID):</label>
      <input type="text" id="devName" placeholder="e.g. camOne">

      <label for="interval">Interval (Seconds):</label>
      <input type="number" id="interval" placeholder="e.g. 300">

      <label for="lightPwm">Light Brightness (1000-2000):</label>
      <input type="number" id="lightPwm" placeholder="e.g. 1500">

      <label for="lightDur">Light Warmup (ms):</label>
      <input type="number" id="lightDur" placeholder="e.g. 1000">

      <button type="submit">Save Settings</button>
    </form>
  </div>

  <div class="card">
    <h2>Gallery</h2>
    <button onclick="loadFiles()">Refresh List</button>
    <table id="fileTable">
      <thead><tr><th>Name</th><th>Size</th><th>Action</th></tr></thead>
      <tbody></tbody>
    </table>
  </div>

<script>
  function fetchStatus() {
    fetch('/status').then(response => response.json()).then(data => {
      document.getElementById('status').innerText = `Device: ${data.name} | Storage: ${data.storage}`;
      document.getElementById('devTime').innerText = data.time;
      document.getElementById('devName').value = data.name;
      document.getElementById('interval').value = data.interval;
      document.getElementById('lightPwm').value = data.lightPwm;
      document.getElementById('lightDur').value = data.lightDur;
    });
  }

  function syncTime() {
    const now = new Date();
    // Send local time as epoch seconds
    const timestamp = Math.floor(now.getTime() / 1000);
    // Also send timezone offset in minutes (JS returns negative for ahead of UTC, so invert)
    const tzOffset = now.getTimezoneOffset() * -1;

    fetch(`/set-time?time=${timestamp}&tz=${tzOffset}`)
      .then(response => response.text())
      .then(alert);
  }

  function startCollection() {
    if(confirm("Start Collection Mode? The device will take photos and sleep.")) {
      fetch('/control?action=start').then(r => alert("Starting... Device will sleep shortly."));
    }
  }

  function shutdown() {
    if(confirm("Shutdown (Deep Sleep)? You will need to use the magnet to wake it.")) {
      fetch('/control?action=shutdown').then(r => alert("Shutting down..."));
    }
  }

  function toggleLight() {
    fetch('/control?action=light').then(r => r.text()).then(alert);
  }

  function takePhoto() {
    document.getElementById('preview-container').innerText = "Capturing...";
    fetch('/capture').then(r => r.blob()).then(blob => {
      const url = URL.createObjectURL(blob);
      document.getElementById('preview-container').innerHTML = `<img src="${url}" class="preview" />`;
    });
  }

  function saveSettings() {
    const name = document.getElementById('devName').value;
    const interval = document.getElementById('interval').value;
    const lightPwm = document.getElementById('lightPwm').value;
    const lightDur = document.getElementById('lightDur').value;

    fetch(`/save-config?name=${name}&interval=${interval}&lightPwm=${lightPwm}&lightDur=${lightDur}`)
      .then(r => r.text())
      .then(alert);
  }

  function loadFiles() {
    fetch('/list').then(r => r.json()).then(files => {
      const tbody = document.querySelector('#fileTable tbody');
      tbody.innerHTML = '';
      files.forEach(f => {
        const row = `<tr>
          <td><a href="${f.name}" target="_blank">${f.name}</a></td>
          <td>${f.size}</td>
          <td><button class="red" style="padding:5px 10px;" onclick="deleteFile('${f.name}')">X</button></td>
        </tr>`;
        tbody.innerHTML += row;
      });
    });
  }

  function deleteFile(fname) {
    if(confirm(`Delete ${fname}?`)) {
      fetch(`/delete?path=${fname}`).then(loadFiles);
    }
  }

  window.onload = function() {
    fetchStatus();
    loadFiles();
    // Auto-sync time if needed?
    // syncTime();
  };
</script>
</body>
</html>
)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>KiloCam Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f4f4f4; }
    h1 { color: #333; }
    .card { background: white; padding: 20px; margin: 10px auto; max-width: 800px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    button { background-color: #008CBA; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin: 5px; }
    button.red { background-color: #f44336; }
    button.green { background-color: #4CAF50; }
    input[type=text], input[type=number] { width: 100%; padding: 10px; margin: 8px 0; display: inline-block; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
    img.preview { max-width: 100%; height: auto; margin-top: 10px; border-radius: 4px; }
    .status-bar { padding: 10px; background: #e7f3fe; border-left: 6px solid #2196F3; margin-bottom: 15px; text-align: left; }
    #gallery-controls { text-align: left; margin-bottom: 10px; }
    .path-display { font-weight: bold; margin-left: 10px; }
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
    <div id="gallery-controls">
        <button onclick="loadDirectory(currentPath)">Refresh</button>
        <button id="backBtn" onclick="goUp()" style="display:none;">Back</button>
        <span id="currentPathDisplay" class="path-display">/</span>
    </div>
    <table id="fileTable">
      <thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead>
      <tbody></tbody>
    </table>
  </div>

<script>
  let currentPath = '/';

  function fetchStatus() {
    fetch('/status').then(response => response.json()).then(data => {
      document.getElementById('status').innerText = `Device: ${data.name} | Storage: ${data.storage}`;
      document.getElementById('devTime').innerText = data.time;
      document.getElementById('interval').value = data.interval;
      document.getElementById('lightPwm').value = data.lightPwm;
      document.getElementById('lightDur').value = data.lightDur;
    });
  }

  function syncTime() {
    const now = new Date();
    const timestamp = Math.floor(now.getTime() / 1000);
    const tzOffset = now.getTimezoneOffset() * -1;
    fetch(`/set-time?time=${timestamp}&tz=${tzOffset}`)
      .then(response => response.text())
      .then(alert);
  }

  function startCollection() {
    if(confirm("Start New Collection Run? This will create a new directory and start the loop.")) {
      fetch('/control?action=start').then(r => r.text()).then(msg => alert(msg));
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
    const interval = document.getElementById('interval').value;
    const lightPwm = document.getElementById('lightPwm').value;
    const lightDur = document.getElementById('lightDur').value;

    fetch(`/save-config?interval=${interval}&lightPwm=${lightPwm}&lightDur=${lightDur}`)
      .then(r => r.text())
      .then(alert);
  }

  function loadDirectory(path) {
    currentPath = path;
    document.getElementById('currentPathDisplay').innerText = currentPath;
    document.getElementById('backBtn').style.display = (currentPath === '/' || currentPath === '') ? 'none' : 'inline-block';

    fetch(`/list?path=${encodeURIComponent(path)}`)
      .then(r => {
        if (!r.ok) throw new Error("Failed to load");
        return r.json();
      })
      .then(files => {
        const tbody = document.querySelector('#fileTable tbody');
        tbody.innerHTML = '';

        // Sort: Directories first
        files.sort((a, b) => (a.isDir === b.isDir) ? 0 : a.isDir ? -1 : 1);

        files.forEach(f => {
          let nameCell = f.name;
          let sizeCell = f.size;
          let actionsCell = '';

          let fullPath = currentPath;
          if (!fullPath.endsWith('/')) fullPath += '/';
          fullPath += f.name;

          if (f.isDir) {
            nameCell = `<b>📁 ${f.name}</b>`;
            sizeCell = '-';
            actionsCell = `
              <button class="green" style="padding:5px;" onclick="loadDirectory('${fullPath}')">Open</button>
              <button style="padding:5px;" onclick="downloadAll('${fullPath}')">Down All</button>
              <button class="red" style="padding:5px;" onclick="deleteItem('${fullPath}', true)">Del</button>
            `;
          } else {
            let fileUrl = fullPath;
            nameCell = `<a href="${fileUrl}" target="_blank">📄 ${f.name}</a>`;
            actionsCell = `<button class="red" style="padding:5px;" onclick="deleteItem('${fullPath}', false)">Del</button>`;
          }

          const row = `<tr>
            <td>${nameCell}</td>
            <td>${sizeCell}</td>
            <td>${actionsCell}</td>
          </tr>`;
          tbody.innerHTML += row;
        });
      })
      .catch(e => {
        console.error(e);
        // alert("Error loading directory");
      });
  }

  function goUp() {
    if (currentPath === '/' || currentPath === '') return;

    // Normalize path to not end in slash unless root
    if (currentPath.endsWith('/') && currentPath.length > 1) {
        currentPath = currentPath.substring(0, currentPath.length - 1);
    }

    let lastSlash = currentPath.lastIndexOf('/');
    let newPath = currentPath.substring(0, lastSlash);
    if (newPath === '') newPath = '/';
    loadDirectory(newPath);
  }

  function deleteItem(path, isDir) {
    if(confirm(`Delete ${isDir ? 'Directory (Recursive!)' : 'File'}: ${path}?`)) {
      fetch(`/delete?path=${encodeURIComponent(path)}`)
        .then(r => {
            if(r.ok) {
                loadDirectory(currentPath);
            }
            else alert("Delete Failed");
        });
    }
  }

  function downloadAll(dirPath) {
    if(!confirm(`Download all files in ${dirPath}? This will open multiple downloads.`)) return;

    fetch(`/list?path=${encodeURIComponent(dirPath)}`)
      .then(r => r.json())
      .then(files => {
        let count = 0;
        files.forEach((f, index) => {
          if(!f.isDir) {
            setTimeout(() => {
                let fullPath = dirPath;
                if (!fullPath.endsWith('/')) fullPath += '/';
                fullPath += f.name;

                const link = document.createElement('a');
                link.href = fullPath;
                link.download = f.name;
                document.body.appendChild(link);
                link.click();
                document.body.removeChild(link);
            }, index * 500); // 500ms delay between downloads
            count++;
          }
        });
        if(count === 0) alert("No files to download.");
      });
  }

  window.onload = function() {
    fetchStatus();
    loadDirectory('/');
  };
</script>
</body>
</html>
)rawliteral";

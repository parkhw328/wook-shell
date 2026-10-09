// Isolated SSH + actual Win32 clipboard, using messages at the terminal's HWND.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const { StringDecoder } = require('node:string_decoder');
const root = path.resolve(__dirname, '../..');
const applicationMouse = process.argv.includes('--application-mouse');
const artifact = path.join(root, 'build', `mouse-test-${Date.now()}`);
fs.mkdirSync(artifact);
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
const key = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem' });
const fingerprint = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/, '');
const signal = name => fs.writeFileSync(path.join(artifact, `mouse-${name}.ready`), 'ready\n');
const clients = new Set();
let received = '', stage = 'screen', timer;
const hoverReports = {};
const server = new Server({ hostKeys: [key] }, client => {
  clients.add(client);
  client.on('error', () => {});
  client.on('close', () => clients.delete(client));
  client.on('authentication', ctx => ctx.username === 'mouse' && ctx.method === 'none' ? ctx.accept() : ctx.reject());
  client.on('ready', () => client.on('session', accept => {
    const session = accept();
    session.on('pty', accept => accept());
    session.on('window-change', accept => { if (accept) accept(); });
    session.on('env', accept => { if (accept) accept(); });
    session.on('shell', accept => {
      const stream = accept(), decoder = new StringDecoder('utf8');
      stream.on('error', () => {});
      const screen = () => stream.write('\x1b[2J\x1b[HCOPY-MOUSE\x1b[H\x1b[6n');
      screen();
      stream.on('data', bytes => {
        received += decoder.write(bytes);
        if (received.includes('\x1b[1;1R')) signal(stage);
        for (const name of ['active', 'shift', 'restored', 'abandoned', 'abandoned-shift']) {
          const marker = `hover-${name}\r`;
          if (!(name in hoverReports) && received.includes(marker)) {
            const preceding = received.slice(0, received.indexOf(marker));
            const previousMarker = preceding.lastIndexOf('hover-');
            const segment = previousMarker < 0 ? preceding : preceding.slice(preceding.indexOf('\r', previousMarker) + 1);
            hoverReports[name] = segment.match(/\x1b\[<\d+;\d+;\d+[mM]/g) || [];
            if (name === 'abandoned') signal('abandoned-input');
          }
        }
        for (const kind of ['normal', 'preferred', 'shift', 'toggled', 'restored', 'abandoned']) {
          if (received.includes(`${kind}-paste-한글`)) signal(`${kind}-paste`);
        }
        if (/\x1b\[<0;\d+;\d+M/.test(received) && /\x1b\[<2;\d+;\d+M/.test(received)) signal(stage === 'toggled' ? 'toggled-raw' : 'raw-clicks');
      });
      timer = setInterval(() => {
        if (stage === 'screen' && fs.existsSync(path.join(artifact, 'mouse-enable.request'))) {
          stage = 'reporting'; received = '';
          stream.write('\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h\x1b[?1004h\x1b[?2004h'); screen();
        } else if (stage === 'reporting' && fs.existsSync(path.join(artifact, 'mouse-toggle.request'))) {
          fs.writeFileSync(path.join(artifact, 'reporting-input.json'), JSON.stringify(received));
          stage = 'toggled'; received = ''; screen();
        } else if (stage === 'toggled' && fs.existsSync(path.join(artifact, 'mouse-disable.request'))) {
          fs.writeFileSync(path.join(artifact, 'toggled-input.json'), JSON.stringify(received));
          stage = 'restored'; received = '';
          stream.write('\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[?1006l\x1b[?1004l\x1b[?2004l'); screen();
        } else if (stage === 'restored' && fs.existsSync(path.join(artifact, 'mouse-abandon.request'))) {
          fs.writeFileSync(path.join(artifact, 'restored-input.json'), JSON.stringify(received));
          stage = 'abandoned'; received = '';
          // A TUI returns to the normal screen without resetting any-event reporting.
          stream.write('\x1b[?1049h\x1b[?1003h\x1b[?1006h\x1b[?1049l'); screen();
        }
      }, 30);
    });
  }));
});
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const environment = {
      ...process.env, WOOK_TEST_MOUSE_ONLY: '1', WOOK_TEST_PORT: String(server.address().port), WOOK_TEST_FINGERPRINT: fingerprint,
    };
    delete environment.WOOK_TEST_APP_MOUSE;
    if (applicationMouse) environment.WOOK_TEST_APP_MOUSE = '1';
    const child = spawn(executable, [], { cwd: artifact, windowsHide: true, stdio: 'ignore', env: environment });
    const timeout = setTimeout(() => child.kill(), 60000);
    const code = await new Promise((resolve, reject) => { child.on('error', reject); child.on('close', resolve); }).finally(() => clearTimeout(timeout));
    const result = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    fs.writeFileSync(path.join(artifact, 'hover-reports.json'), JSON.stringify(hoverReports, null, 2));
    console.log(JSON.stringify({ artifact, applicationMouse, code, ...result }, null, 2));
    assert.equal(code, 0, 'Mouse test process must exit successfully');
    assert.equal(result.passed, true, JSON.stringify(result));
    const reporting = JSON.parse(fs.readFileSync(path.join(artifact, 'reporting-input.json'), 'utf8'));
    if (applicationMouse) {
      assert.ok(!reporting.includes('raw-paste-sentinel'), 'Opting out must deliver unmodified right clicks to the application');
      assert.match(reporting, /\x1b\[<0;\d+;\d+M/, 'Opting out must deliver left clicks');
      assert.match(reporting, /\x1b\[<2;\d+;\d+M/, 'Opting out must deliver right clicks');
    } else {
      assert.ok(!/\x1b\[<\d+;\d+;\d+[mM]/.test(reporting), 'Clipboard priority must not leak selection mouse events to the application');
      assert.equal(reporting.split('\x1b[200~preferred-paste-한글\x1b[201~').length - 1, 1, 'Plain right-click must send exactly one bracketed Unicode paste');
    }
    assert.equal(reporting.split('\x1b[200~shift-paste-한글\x1b[201~').length - 1, 1, 'Shift-right-click must send exactly one bracketed Unicode paste');
    const settings = JSON.parse(fs.readFileSync(path.join(artifact, 'settings-live-1.json'), 'utf8'));
    assert.equal(settings.passed, true, JSON.stringify(settings));
    const toggled = JSON.parse(fs.readFileSync(path.join(artifact, 'toggled-input.json'), 'utf8'));
    if (applicationMouse) {
      assert.ok(!/\x1b\[<\d+;\d+;\d+[mM]/.test(toggled), 'Enabling clipboard priority live must stop application mouse events');
      assert.equal(toggled.split('\x1b[200~toggled-paste-한글\x1b[201~').length - 1, 1, 'Enabling priority live must paste once');
    } else {
      assert.ok(!toggled.includes('toggled-paste-sentinel'), 'Disabling priority live must stop plain right-click paste');
      assert.match(toggled, /\x1b\[<0;\d+;\d+M/, 'Disabling priority live must restore left clicks');
      assert.match(toggled, /\x1b\[<2;\d+;\d+M/, 'Disabling priority live must restore right clicks');
    }
    const restored = JSON.parse(fs.readFileSync(path.join(artifact, 'restored-input.json'), 'utf8'));
    assert.ok(restored.includes('restored-paste-한글') && !restored.includes('\x1b[200~'), 'Plain paste resumes after mode reset');
    assert.equal(hoverReports.active.length, applicationMouse ? 10 : 0, 'Only explicit application mouse mode sends passive movement');
    assert.deepEqual(hoverReports.shift, [], 'Shift must suppress passive movement as well as clicks');
    assert.deepEqual(hoverReports.restored, [], 'No movement reports after normal TUI cleanup');
    assert.equal(hoverReports.abandoned.length, applicationMouse ? 0 : 10, 'Reproduce stale reporting only when application mouse mode is allowed');
    assert.deepEqual(hoverReports['abandoned-shift'], [], 'Shift must also suppress movement when the TUI leaves mouse reporting enabled');
    assert.ok(received.includes('35;68;23M\r'), 'Do not remove literal report-looking keyboard input');
    console.log('Mouse capture, Shift override, focus return and bracketed paste checks passed.');
  } finally {
    clearInterval(timer); for (const client of clients) client.destroy(); server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

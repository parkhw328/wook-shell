// Real loopback SSH output and the native terminal scrollbar, with wheel messages.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const followOutput = process.argv.includes('--follow-output');
const blockMouse = process.argv.includes('--block-mouse');
const noMouse = process.argv.includes('--no-mouse');
const artifact = path.join(root, 'build', `scroll-test-${Date.now()}`);
fs.mkdirSync(artifact);
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
const key = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem' });
const fingerprint = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/, '');
const clients = new Set();
let timer, received = '', allInput = '', stage = 'screen', acknowledged = false;
const server = new Server({ hostKeys: [key] }, client => {
  clients.add(client);
  client.on('error', () => {});
  client.on('close', () => clients.delete(client));
  client.on('authentication', ctx => ctx.username === 'scroll' && ctx.method === 'none' ? ctx.accept() : ctx.reject());
  client.on('ready', () => client.on('session', accept => {
    const session = accept();
    session.on('pty', accept => accept());
    session.on('window-change', accept => { if (accept) accept(); });
    session.on('env', accept => { if (accept) accept(); });
    session.on('shell', accept => {
      const stream = accept(); stream.on('error', () => {});
      const lines = count => Array.from({ length: count }, (_, i) => `History line ${stage} ${String(i).padStart(3, '0')}\r\n`).join('');
      // Request raw mouse tracking too: clipboard priority must keep the wheel local.
      stream.write('\x1b[?1003h\x1b[?1006h' + lines(180) + '\x1b[6n');
      stream.on('data', bytes => {
        received += bytes.toString('utf8');
        allInput += bytes.toString('utf8');
        for (const [button, name] of [[64, 'up'], [65, 'down']]) {
          if (new RegExp(`\\x1b\\[<${button};\\d+;\\d+M`).test(received) &&
              !fs.existsSync(path.join(artifact, `scroll-app-${name}.ready`))) {
            stream.write('\x1b[H\x1b[2K' + (name === 'up' ? 'Back to bottom' : 'Latest conversation') + '\x1b[H');
            fs.writeFileSync(path.join(artifact, `scroll-app-${name}.ready`), 'ready\n');
          }
        }
        if (/\x1b\[<0;5;1M/.test(received) && /\x1b\[<0;5;1m/.test(received) &&
            !fs.existsSync(path.join(artifact, 'scroll-app-bottom.ready'))) {
          stream.write('\x1b[H\x1b[2KLatest conversation restored by button click\x1b[H');
          fs.writeFileSync(path.join(artifact, 'scroll-app-bottom.ready'), 'ready\n');
        }
        if (!acknowledged && /\x1b\[\d+;\d+R/.test(received)) {
          acknowledged = true;
          fs.writeFileSync(path.join(artifact, `scroll-${stage}.ready`), 'ready\n');
        }
      });
      const stages = [
        ['output', () => lines(8) + '\x1b[1G\x1b[2KWorking...'],
        ['tail', () => '\r\n' + lines(8)],
        ['alternate', () => (noMouse ? '\x1b[?1003l' : '') + '\x1b[?1049h\x1b[2J\x1b[HAlternate screen running'],
        ['redraw', () => '\x1b[2;1H\x1b[2KProgress update\x1b[4;2H\x1b[?25l\x1b[?25h'],
        ['normal', () => '\x1b[?1049l'], // Deliberately leave mouse reporting enabled.
      ];
      let next = 0;
      timer = setInterval(() => {
        if (next === stages.length) return;
        const [name, content] = stages[next];
        if (!fs.existsSync(path.join(artifact, `scroll-${name}.request`))) return;
        stage = name; received = ''; acknowledged = false; ++next;
        stream.write(content() + '\x1b[6n');
      }, 30);
    });
  }));
});
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const environment = { ...process.env, WOOK_TEST_SCROLL_ONLY: '1', WOOK_TEST_PORT: String(server.address().port), WOOK_TEST_FINGERPRINT: fingerprint };
    delete environment.WOOK_TEST_FOLLOW_OUTPUT;
    delete environment.WOOK_TEST_LOCAL_WHEEL;
    delete environment.WOOK_TEST_BLOCK_MOUSE;
    if (blockMouse || noMouse) environment.WOOK_TEST_LOCAL_WHEEL = '1';
    if (blockMouse) environment.WOOK_TEST_BLOCK_MOUSE = '1';
    if (followOutput) environment.WOOK_TEST_FOLLOW_OUTPUT = '1';
    const child = spawn(executable, [], { cwd: artifact, windowsHide: true, stdio: 'ignore', env: environment });
    const timeout = setTimeout(() => child.kill(), 60000);
    const code = await new Promise((resolve, reject) => { child.on('error', reject); child.on('close', resolve); }).finally(() => clearTimeout(timeout));
    const result = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    console.log(JSON.stringify({ artifact, followOutput, blockMouse, noMouse, code, ...result }, null, 2));
    assert.equal(code, 0, 'Scroll test process must exit successfully');
    assert.equal(result.passed, true, JSON.stringify(result));
    assert.equal((allInput.match(/\r/g) || []).length, 3, 'Each Enter must reach SSH exactly once');
    assert.equal(stage, 'normal', 'Both normal and alternate screens and return must be exercised');
    const reports = [...allInput.matchAll(/\x1b\[<(\d+);\d+;\d+([mM])/g)].map(m => [Number(m[1]), m[2]]);
    assert.deepEqual(reports, blockMouse || noMouse ? [] : [[64, 'M'], [0, 'M'], [0, 'm'], [65, 'M']], 'Application wheel and button press/release must arrive exactly once');
    console.log('Scrollback retention, partial wheel deltas, Enter/keypad Enter return to live output and alternate-screen repaint checks passed.');
  } finally {
    clearInterval(timer); for (const client of clients) client.destroy(); server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

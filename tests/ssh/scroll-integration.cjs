// Real loopback SSH output and the native terminal scrollbar, with wheel messages.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const followOutput = process.argv.includes('--follow-output');
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
        if (!acknowledged && /\x1b\[\d+;\d+R/.test(received)) {
          acknowledged = true;
          fs.writeFileSync(path.join(artifact, `scroll-${stage}.ready`), 'ready\n');
        }
      });
      const stages = [
        ['output', () => lines(8) + '\x1b[1G\x1b[2KWorking...'],
        ['tail', () => '\r\n' + lines(8)],
        ['alternate', () => '\x1b[?1049h\x1b[2J\x1b[HAlternate screen running'],
        ['redraw', () => '\x1b[2;1H\x1b[2KProgress update\x1b[4;2H\x1b[?25l\x1b[?25h'],
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
    if (followOutput) environment.WOOK_TEST_FOLLOW_OUTPUT = '1';
    const child = spawn(executable, [], { cwd: artifact, windowsHide: true, stdio: 'ignore', env: environment });
    const timeout = setTimeout(() => child.kill(), 60000);
    const code = await new Promise((resolve, reject) => { child.on('error', reject); child.on('close', resolve); }).finally(() => clearTimeout(timeout));
    const result = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    console.log(JSON.stringify({ artifact, followOutput, code, ...result }, null, 2));
    assert.equal(code, 0, 'Scroll test process must exit successfully');
    assert.equal(result.passed, true, JSON.stringify(result));
    assert.equal(stage, 'redraw', 'Both normal and alternate screens must be exercised');
    assert.ok(!/\x1b\[<\d+;\d+;\d+[mM]/.test(allInput), 'Local wheel scrolling must not send mouse input over SSH');
    console.log('Scrollback retention, partial wheel deltas, return to live output and alternate-screen repaint checks passed.');
  } finally {
    clearInterval(timer); for (const client of clients) client.destroy(); server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

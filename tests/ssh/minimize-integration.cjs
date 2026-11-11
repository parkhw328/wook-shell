// Observe actual SSH PTY requests while the native workspace is minimized.
// Optional Wine execution is compatibility evidence, not Windows verification.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const { parseArgs } = require('node:util');
const { values } = parseArgs({ options: {
  binaries: { type: 'string', default: 'build/native' }, wine: { type: 'string' },
  'layout-only': { type: 'boolean' }, 'ssh-only': { type: 'boolean' },
} });
assert.ok(!(values['layout-only'] && values['ssh-only']), 'Choose at most one test subset');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `minimize-test-${Date.now()}`);
fs.mkdirSync(artifact, { recursive: true });
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.resolve(root, values.binaries, 'ui-smoke-tests.exe'), executable);
const key = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem' });
const fingerprint = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/, '');
const clients = new Set(), events = [], snapshots = {};
let outputLines = 0, outputWhileMinimized = 0, monitoring = false;
const server = new Server({ hostKeys: [key] }, client => {
  clients.add(client);
  client.on('error', () => {});
  client.on('close', () => clients.delete(client));
  client.on('authentication', ctx => ctx.username === 'minimize' && ctx.method === 'none' ? ctx.accept() : ctx.reject());
  client.on('ready', () => client.on('session', accept => {
    const session = accept();
    let size, input = '';
    session.on('pty', (accept, reject, info) => {
      size = { cols: info.cols, rows: info.rows }; events.push({ kind: 'pty', ...size }); accept();
    });
    session.on('window-change', (accept, reject, info) => {
      size = { cols: info.cols, rows: info.rows };
      events.push({ kind: 'resize', monitoring, ...size }); if (accept) accept();
    });
    session.on('shell', accept => {
      const stream = accept();
      const timer = setInterval(() => {
        ++outputLines; if (monitoring) ++outputWhileMinimized;
        stream.write(`line ${outputLines}: SSH output continues across the full terminal width 0123456789\r\n`);
      }, 100);
      stream.on('close', () => clearInterval(timer)); stream.on('error', () => clearInterval(timer));
      stream.on('data', data => {
        input += data.toString('utf8');
        let end;
        while ((end = input.indexOf('\r')) >= 0) {
          const command = input.slice(0, end); input = input.slice(end + 1);
          if (!['baseline', 'minimized', 'restored', 'resized'].includes(command)) continue;
          snapshots[command] = { ...size };
          if (command === 'baseline') monitoring = true;
          if (command === 'restored') monitoring = false;
          fs.writeFileSync(path.join(artifact, `${command}.ready`), 'ready\n');
        }
      });
    });
  }));
});
(async () => {
  try {
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    const env = { ...process.env, WOOK_TEST_MINIMIZE_ONLY: '1', WOOK_TEST_FINGERPRINT: fingerprint,
      WOOK_DATA_DIR: values.wine ? 'Z:' + artifact.replaceAll('/', '\\') + '\\data' : path.join(artifact, 'data') };
    delete env.WOOK_TEST_MINIMIZE_SSH_ONLY; delete env.WOOK_TEST_PORT;
    if (values['ssh-only']) env.WOOK_TEST_MINIMIZE_SSH_ONLY = '1';
    if (!values['layout-only']) env.WOOK_TEST_PORT = String(server.address().port);
    const child = spawn(values.wine || executable, values.wine ? [executable] : [], {
      cwd: artifact, windowsHide: false, stdio: ['ignore', 'ignore', 'pipe'],
      env,
    });
    const log = fs.createWriteStream(path.join(artifact, 'stderr.log')); child.stderr.pipe(log);
    const timeout = setTimeout(() => child.kill(), 90000);
    const code = await new Promise((resolve, reject) => { child.once('error', reject); child.once('close', resolve); })
      .finally(() => clearTimeout(timeout));
    const report = fs.readFileSync(path.join(artifact, 'minimize-result.txt'), 'utf8').trim();
    fs.writeFileSync(path.join(artifact, 'pty-events.json'), JSON.stringify({ events, snapshots, outputLines, outputWhileMinimized }, null, 2));
    console.log(JSON.stringify({ artifact, runtime: values.wine ? 'Wine' : 'Windows', code, report, snapshots }, null, 2));
    assert.equal(code, 0); assert.match(report, /^PASS \d+ checks$/);
    if (!values['layout-only']) {
      assert.ok(snapshots.baseline.cols > 40 && snapshots.baseline.rows > 10, 'Start with a usable PTY size');
      assert.deepEqual(snapshots.minimized, snapshots.baseline, 'Minimization must preserve SSH columns and rows');
      assert.deepEqual(snapshots.restored, snapshots.baseline, 'Restore must preserve SSH columns and rows');
      for (const event of events.filter(event => event.monitoring))
        assert.deepEqual({ cols: event.cols, rows: event.rows }, snapshots.baseline, 'No transient shrunken PTY during minimization');
      assert.ok(snapshots.resized.cols > snapshots.baseline.cols && snapshots.resized.rows > snapshots.baseline.rows,
        'An actual resize after restore must still reach the SSH server');
      assert.ok(outputWhileMinimized >= 20, 'SSH output must continue through multiple background timer ticks');
    }
  } finally {
    for (const client of clients) client.destroy();
    server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

// Real SSH transport against an isolated loopback-only fixture; no external host.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', 'ssh-test-' + Date.now());
fs.mkdirSync(artifact, { recursive: true });
const data = path.join(artifact, 'data');
const binaries = path.join(root, 'dist', 'wshell-0.1.0-win-x64');
const password = crypto.randomBytes(24).toString('hex');
const passwordFile = path.join(artifact, 'test-password.txt');
fs.writeFileSync(passwordFile, password + '\n');
const key = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem' });
const fingerprint = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/, '');
const events = { authenticated: [], shells: 0, terminalTypes: [], environment: [], resizes: 0, received: '' };
const clients = new Set();
const server = new Server({ hostKeys: [key] }, client => {
  clients.add(client);
  client.on('error', () => {});
  client.on('close', () => clients.delete(client));
  client.on('authentication', ctx => {
    if ((ctx.username === 'none' || ctx.username === 'shell') && ctx.method === 'none') ctx.accept();
    else if (ctx.username === 'password' && ctx.method === 'password' && ctx.password === password) ctx.accept();
    else ctx.reject(['password']);
  });
  client.on('ready', () => {
    events.authenticated.push(true);
    client.on('session', accept => {
      const session = accept();
      session.on('pty', (accept, reject, info) => { events.terminalTypes.push(info.term); accept(); });
      session.on('env', (accept, reject, info) => { events.environment.push(info); if (accept) accept(); });
      session.on('window-change', (accept) => { ++events.resizes; if (accept) accept(); });
      session.on('exec', (accept, reject, info) => {
        const stream = accept();
        stream.write('WOOK_SSH_OK ' + info.command + '\n');
        stream.exit(0); stream.end();
      });
      session.on('shell', accept => {
        ++events.shells;
        const stream = accept();
        stream.write('\x1b[2J\x1b[H\r\n  \x1b[38;2;58;169;159mSSH CONNECTED\x1b[0m  /  encrypted loopback test\r\n\r\n');
        stream.write('  PuTTY engine + wShell workspace\r\n  UTF-8: 한글 서버 / 日本語 / café\r\n\r\n');
        stream.write('  \x1b[31mANSI\x1b[0m   \x1b[38;5;172m256 colors\x1b[0m   \x1b[38;2;139;126;200m24-bit color\x1b[0m\r\n\r\n');
        stream.write('  developer@loopback  $ ');
        stream.on('data', bytes => { events.received += bytes.toString(); stream.write(bytes); });
        stream.on('error', () => {});
      });
    });
  });
});
function run(executable, args, input = '', environment = {}) {
  console.log('Running ' + path.basename(executable) + ' : ' + args.at(-1));
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, {
      cwd: binaries, windowsHide: !executable.endsWith('ui-smoke-tests.exe'),
      env: { ...process.env, WOOK_DATA_DIR: data, ...environment },
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    let stdout = '', stderr = '';
    const timer = setTimeout(() => { child.kill(); reject(new Error('Timed out: ' + path.basename(executable) + '\n' + stdout + '\n' + stderr)); }, 45000);
    child.stdout.on('data', chunk => stdout += chunk);
    child.stderr.on('data', chunk => stderr += chunk);
    child.on('error', reject);
    child.on('close', code => { clearTimeout(timer); resolve({ code, stdout, stderr }); });
    child.stdin.on('error', () => {});
    child.stdin.end(input);
  });
}
function registryDigest() {
  const result = spawnSync('reg.exe', ['query', 'HKCU\\Software\\SimonTatham\\PuTTY', '/s'], { windowsHide: true });
  return crypto.createHash('sha256').update(result.stdout || '').update(String(result.status)).digest('hex');
}
(async () => {
  const before = registryDigest();
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const plink = path.join(binaries, 'plink.exe');
  const base = ['-ssh', '-P', String(port), '-l', 'none', '127.0.0.1'];
  let result = await run(plink, ['-batch', ...base, 'unknown']);
  assert.notEqual(result.code, 0, 'Unknown host must fail without interactive trust');
  result = await run(plink, ['-batch', '-hostkey', fingerprint, ...base, 'pinned']);
  assert.equal(result.code, 0, result.stderr); assert.match(result.stdout, /WOOK_SSH_OK pinned/);
  result = await run(plink, ['-batch', '-hostkey', 'SHA256:' + Buffer.alloc(32, 1).toString('base64').replace(/=+$/, ''), ...base, 'mismatch']);
  assert.notEqual(result.code, 0, 'A wrong pinned host key must fail');
  result = await run(plink, ['-batch', '-hostkey', fingerprint, '-pwfile', passwordFile, '-ssh', '-P', String(port), '-l', 'password', '127.0.0.1', 'password']);
  assert.equal(result.code, 0, result.stderr); assert.match(result.stdout, /WOOK_SSH_OK password/);
  // Consent here applies only to our generated ephemeral loopback host.
  result = await run(plink, ['-legacy-stdio-prompts', '-no-antispoof', ...base, 'cache'], 'y\n');
  assert.equal(result.code, 0, result.stderr);
  result = await run(plink, ['-batch', ...base, 'cached']);
  assert.equal(result.code, 0, result.stderr); assert.match(result.stdout, /WOOK_SSH_OK cached/);
  assert.ok(fs.existsSync(path.join(data, 'trust')), 'Host trust must persist inside portable data');
  assert.equal(registryDigest(), before, 'Existing PuTTY registry must not change');

  const ui = path.join(root, 'build', 'app', 'ui-smoke-tests.exe');
  fs.copyFileSync(path.join(binaries, 'wook-putty.exe'), path.join(root, 'build', 'app', 'wook-putty.exe'));
  fs.cpSync(path.join(binaries, 'fonts'), path.join(root, 'build', 'app', 'fonts'), { recursive: true });
  fs.cpSync(path.join(binaries, 'assets'), path.join(root, 'build', 'app', 'assets'), { recursive: true });
  result = await run(ui, [], '', { WOOK_TEST_PORT: String(port), WOOK_TEST_FINGERPRINT: fingerprint });
  const report = JSON.parse(fs.readFileSync(path.join(root, 'build', 'app', 'ui-smoke-result.json'), 'utf8'));
  assert.equal(report.passed, true, JSON.stringify(report));
  assert.ok(events.shells >= 1, 'GUI terminal must authenticate and open a real SSH shell');
  assert.ok(events.terminalTypes.includes('xterm-256color'), 'PTY terminal type must support color');
  assert.ok(events.environment.some(e => e.key === 'COLORTERM' && e.val === 'truecolor'), 'True Color environment must be requested');
  assert.match(events.received, /x/, 'Keyboard data must cross the real SSH connection');
  assert.ok(events.resizes >= 1, 'PTY resize must reach the server');
  assert.equal(registryDigest(), before, 'GUI must leave existing PuTTY registry unchanged');
  const summary = { passed: true, sshChecks: 12, ui: report, events };
  fs.writeFileSync(path.join(artifact, 'result.json'), JSON.stringify(summary, null, 2));
  console.log(JSON.stringify(summary, null, 2));
  console.log('Evidence: ' + artifact);
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => {
  for (const client of clients) client.end();
  server.close();
  fs.unlinkSync(passwordFile);
});

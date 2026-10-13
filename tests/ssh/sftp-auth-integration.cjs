// Requires an unlocked interactive Windows desktop: the GUI uses SendInput.
const { Server, utils } = require('ssh2');
const { spawn } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const attachSftp = require('./sftp-fixture.cjs');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `sftp-auth-test-${Date.now()}`);
const remote = path.join(artifact, 'remote');
fs.mkdirSync(remote, { recursive: true });
fs.writeFileSync(path.join(remote, 'authenticated.txt'), 'OTP fixture\n');
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
const hostKey = utils.generateKeyPairSync('ed25519').private;
const pin = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(hostKey).getPublicSSH()).digest('base64').replace(/=+$/, '');
const clients = new Set(), attempts = [], responses = [], authenticated = [], sftp = {};
const server = new Server({ hostKeys: [hostKey] }, client => {
  clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
  let user;
  client.on('authentication', ctx => {
    if (ctx.method !== 'keyboard-interactive') return ctx.reject(['keyboard-interactive']);
    user = ctx.username; attempts.push(user);
    const prompts = [{ prompt: 'One-time code: ', echo: false }];
    if (user === 'otp') prompts.push({ prompt: 'Second authentication code: ', echo: false });
    ctx.prompt(prompts, 'Loopback OTP fixture', values => {
      if (values instanceof Error) return; // ssh2 reports aborted challenges through this callback.
      responses.push({ user, values });
      if (user === 'otp' && values.length === 2 && values[0] === '123456' && values[1] === '654321') ctx.accept();
      else ctx.reject();
    });
  });
  client.on('ready', () => {
    authenticated.push(user);
    client.on('session', accept => attachSftp(accept(), remote, sftp));
  });
});
(async () => {
  try {
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    const child = spawn(executable, [], {
      cwd: artifact, windowsHide: false, stdio: 'ignore',
      env: { ...process.env, WOOK_TEST_SFTP_AUTH_ONLY: '1', WOOK_TEST_PORT: String(server.address().port), WOOK_TEST_FINGERPRINT: pin },
    });
    const timer = setTimeout(() => child.kill(), 90000);
    const code = await new Promise((resolve, reject) => { child.on('close', resolve); child.on('error', reject); }).finally(() => clearTimeout(timer));
    const report = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    console.log(JSON.stringify({ artifact, code, attempts, authenticated, ...report }, null, 2));
    assert.equal(code, 0); assert.equal(report.passed, true, JSON.stringify(report));
    assert.deepEqual(attempts, ['otp', 'cancel', 'background']);
    assert.deepEqual(responses, [{ user: 'otp', values: ['123456', '654321'] }], 'Each code must reach the server once; cancelled prompts must send no answer');
    assert.deepEqual(authenticated, ['otp']); assert.equal(sftp.connections, 1);
    console.log('PASS: multi-prompt OTP, Enter focus return, Esc cancellation and background focus preservation.');
  } finally {
    for (const client of clients) client.destroy();
    server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

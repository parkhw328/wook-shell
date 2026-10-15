// Windows native UI + independent SSH/SFTP server; no real accounts or files.
const { Server, utils } = require('ssh2');
const { spawn } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const attachSftp = require('./sftp-fixture.cjs');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `sftp-error-test-${Date.now()}`);
const remote = path.join(artifact, 'remote');
fs.mkdirSync(remote, { recursive: true });
for (const name of ['denied-open.bin', 'denied-read.bin']) fs.writeFileSync(path.join(remote, name), Buffer.alloc(65536, 42));
for (const name of ['allowed.txt', 'after-reconnect.txt']) fs.writeFileSync(path.join(remote, name), 'Permission recovery · 한글\n');
fs.mkdirSync(path.join(remote, 'remote-folder'));
fs.writeFileSync(path.join(remote, 'remote-folder', 'remote-marker.txt'), 'remote');
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
const hostKey = utils.generateKeyPairSync('ed25519').private;
const pin = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(hostKey).getPublicSSH()).digest('base64').replace(/=+$/, '');
const clients = new Set();
const events = { denyOpen: ['denied-open.bin'], denyRead: ['denied-read.bin'] };
const server = new Server({ hostKeys: [hostKey] }, client => {
  clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
  client.on('authentication', ctx => ctx.username === 'none' && ctx.method === 'none' ? ctx.accept() : ctx.reject());
  client.on('ready', () => client.on('session', accept => attachSftp(accept(), remote, events)));
});
(async () => {
  try {
    await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
    const child = spawn(executable, [], { cwd: artifact, stdio: 'ignore', env: {
      ...process.env, WOOK_TEST_SFTP_ERROR_ONLY: '1', WOOK_TEST_PORT: String(server.address().port), WOOK_TEST_FINGERPRINT: pin,
    } });
    const timer = setTimeout(() => child.kill(), 60000);
    const code = await new Promise((resolve, reject) => { child.on('close', resolve); child.on('error', reject); }).finally(() => clearTimeout(timer));
    const report = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    console.log(JSON.stringify({ artifact, code, ...report }, null, 2));
    assert.equal(code, 0); assert.equal(report.passed, true, JSON.stringify(report));
    assert.equal(events.connections, 2, 'OPEN denial must reuse the session; pipeline failure must reconnect once');
    for (const name of ['allowed.txt', 'after-reconnect.txt'])
      assert.deepEqual(fs.readFileSync(path.join(artifact, 'downloads', name)), fs.readFileSync(path.join(remote, name)));
    assert.deepEqual(fs.readdirSync(path.join(artifact, 'downloads')).sort(), ['after-reconnect.txt', 'allowed.txt']);
    console.log('PASS: permission errors, interactive controls, retry, F5 reconnect and exact downloaded bytes');
  } finally {
    for (const client of clients) client.destroy();
    server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

// Real SFTP traffic with delayed acknowledgements keeps the Cancel button test deterministic.
const { Server, utils } = require('ssh2');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const attachSftp = require('./sftp-fixture.cjs');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `sftp-cancel-${Date.now()}`);
const remote = path.join(artifact, 'remote'), local = path.join(artifact, 'local');
fs.mkdirSync(remote, { recursive: true }); fs.mkdirSync(local);
for (const file of [path.join(remote, 'slow.bin'), path.join(local, 'upload.bin')]) {
  const fd = fs.openSync(file, 'w'); fs.ftruncateSync(fd, 64 * 1024 * 1024); fs.closeSync(fd);
}
fs.writeFileSync(path.join(remote, 'allowed.txt'), 'download after cancellation\n');
fs.writeFileSync(path.join(remote, 'zz-never-downloaded.bin'), 'queued download');
fs.writeFileSync(path.join(local, 'zz-never-uploaded.bin'), 'queued upload');
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
const hostKey = utils.generateKeyPairSync('ed25519').private;
const pin = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(hostKey).getPublicSSH()).digest('base64').replace(/=+$/, '');
const events = { readDelay: 500, writeDelay: 500, readLimit: 32768 }, clients = new Set();
const server = new Server({ hostKeys: [hostKey] }, client => {
  clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
  client.on('authentication', ctx => ctx.username === 'none' && ctx.method === 'none' ? ctx.accept() : ctx.reject());
  client.on('ready', () => client.on('session', accept => attachSftp(accept(), remote, events)));
});
(async () => {
  try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const child = spawn(executable, [], { cwd: artifact, stdio: 'ignore', env: {
      ...process.env, WOOK_TEST_SFTP_CANCEL_ONLY: '1', WOOK_TEST_PORT: String(server.address().port), WOOK_TEST_FINGERPRINT: pin,
    } });
    const timer = setTimeout(() => child.kill(), 60000);
    const code = await new Promise((resolve, reject) => { child.on('close', resolve); child.on('error', reject); }).finally(() => clearTimeout(timer));
    const report = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    assert.equal(code, 0); assert.equal(report.passed, true, JSON.stringify(report));
    assert.equal(events.connections, 3, 'Reconnect once after each cancellation');
    assert.ok(!fs.existsSync(path.join(remote, 'upload.bin')) && !fs.existsSync(path.join(remote, 'zz-never-uploaded.bin')));
    assert.ok(!fs.existsSync(path.join(local, 'slow.bin')) && !fs.existsSync(path.join(local, 'zz-never-downloaded.bin')));
    assert.deepEqual(fs.readFileSync(path.join(local, 'allowed.txt')), fs.readFileSync(path.join(remote, 'allowed.txt')));
    assert.ok(!fs.readdirSync(local).some(name => name.startsWith('.wshell-')), 'Local partial downloads are removed');
    console.log(JSON.stringify({ artifact, ...report, connections: events.connections }));
  } finally {
    for (const client of clients) client.destroy(); server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

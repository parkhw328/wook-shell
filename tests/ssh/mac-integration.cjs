// Real AppKit + PTY + OpenSSH against an isolated loopback-only server.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const output = path.join(root, 'build', 'mac-evidence');
const data = path.join(output, 'data');
fs.mkdirSync(data, { recursive: true });
const key = utils.generateKeyPairSync('ed25519');
const serverKey = utils.generateKeyPairSync('ed25519');
const publicKey = utils.parseKey(key.private);
const password = crypto.randomBytes(24).toString('hex');
const keyFile = path.join(output, 'fixture-key'); fs.writeFileSync(keyFile, key.private, { mode: 0o600 });
const events = { signatures: 0, passwords: 0, shells: 0, resizes: 0, input: '', terminalTypes: [] };
const clients = new Set();
const server = new Server({ hostKeys: [serverKey.private] }, client => {
  clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
  client.on('authentication', ctx => {
    if (ctx.username === 'key' && ctx.method === 'publickey' && ctx.key.data.equals(publicKey.getPublicSSH()) &&
        (!ctx.signature || publicKey.verify(ctx.blob, ctx.signature, ctx.hashAlgo) === true)) {
      if (ctx.signature) ++events.signatures; ctx.accept();
    } else if (ctx.username === 'password' && ctx.method === 'password' && ctx.password === password) {
      ++events.passwords; ctx.accept();
    } else ctx.reject(['publickey', 'password']);
  });
  client.on('ready', () => client.on('session', accept => {
    const session = accept();
    session.on('pty', (accept, reject, info) => { events.terminalTypes.push(info.term); accept(); });
    session.on('env', accept => { if (accept) accept(); });
    session.on('window-change', accept => { ++events.resizes; if (accept) accept(); });
    session.on('shell', accept => {
      ++events.shells; const stream = accept();
      stream.write('\r\n\x1b[38;2;218;112;44mwShell · loopback SSH\x1b[0m\r\nWSHELL_SSH_READY\r\n$ ');
      stream.on('data', data => { events.input += data.toString(); stream.write(data); });
    });
  }));
});
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const host = utils.parseKey(serverKey.private);
  fs.writeFileSync(path.join(data, 'known_hosts'), `[127.0.0.1]:${port} ${host.type} ${host.getPublicSSH().toString('base64')}\n`);
  try {
    await new Promise((resolve, reject) => {
      const child = spawn(path.join(root, 'build/mac/wShell.app/Contents/MacOS/wShell'), ['--smoke-test', output], {
        env: { ...process.env, WOOK_DATA_DIR: data, WSHELL_TEST_PORT: String(port), WSHELL_TEST_KEY: keyFile, WSHELL_TEST_PASSWORD: password,
          WSHELL_TEST_HOST_SHA256: 'SHA256:' + crypto.createHash('sha256').update(host.getPublicSSH()).digest('base64').replace(/=+$/, '') }, stdio: 'inherit'
      });
      const timer = setTimeout(() => { child.kill('SIGKILL'); reject(new Error('macOS smoke test timed out')); }, 180000);
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.once('exit', code => { clearTimeout(timer); code === 0 ? resolve() : reject(new Error(`macOS app exited ${code}`)); });
    });
    assert(events.signatures >= 1, 'OpenSSH public-key signature must verify');
    assert.equal(events.passwords, 1, 'Saved password must authenticate exactly once');
    assert.equal(events.shells, 2); assert(events.resizes > 0);
    assert(events.input.includes('key-input') && events.input.includes('password-input'));
    assert(events.terminalTypes.every(type => type === 'xterm-256color'));
    fs.writeFileSync(path.join(output, 'ssh-result.json'), JSON.stringify({ passed: true, events }, null, 2));
    console.log(JSON.stringify({ passed: true, events }));
  } finally {
    for (const client of clients) client.end(); server.close();
    fs.rmSync(keyFile, { force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

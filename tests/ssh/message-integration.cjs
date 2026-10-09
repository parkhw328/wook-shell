// Dialog controls, keyboard messages, and real isolated SSH host trust decisions.
const { Server, utils } = require('ssh2');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `message-test-${Date.now()}`);
fs.mkdirSync(artifact);
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
const clients = new Set(), servers = [], authenticated = [0,0,0];
(async () => {
  try {
    const env = { ...process.env, WOOK_TEST_MESSAGES_ONLY: '1' };
    for (let index = 0; index < 3; ++index) {
      const server = new Server({ hostKeys: [utils.generateKeyPairSync('ed25519').private] }, client => {
        clients.add(client); client.on('error', () => {});
        client.on('close', () => {
          clients.delete(client);
          if (index === 0) fs.writeFileSync(path.join(artifact, 'dialog-finished-0.ready'), 'closed\n');
        });
        client.on('authentication', ctx => {
          ++authenticated[index];
          if (ctx.method === 'none' && ctx.username === 'dialog-user') ctx.accept(); else ctx.reject();
        });
        client.on('ready', () => client.on('session', accept => {
          const session = accept(); session.on('pty', accept => accept());
          session.on('env', accept => { if (accept) accept(); });
          session.on('window-change', accept => { if (accept) accept(); });
          session.on('shell', accept => {
            const stream = accept(); stream.on('error', () => {}); stream.write('SSH dialog fixture\r\n');
            fs.writeFileSync(path.join(artifact, `dialog-finished-${index}.ready`), 'connected\n');
          });
        }));
      });
      servers.push(server); await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
      env[`WOOK_DIALOG_PORT_${index}`] = String(server.address().port);
    }
    const child = spawn(executable, [], { cwd: artifact, windowsHide: true, stdio: 'ignore', env });
    const timer = setTimeout(() => child.kill(), 90000);
    const code = await new Promise((resolve, reject) => { child.on('close', resolve); child.on('error', reject); }).finally(() => clearTimeout(timer));
    const report = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    console.log(JSON.stringify({ artifact, code, authenticated, ...report }, null, 2));
    assert.equal(code, 0); assert.equal(report.passed, true, JSON.stringify(report));
    assert.deepEqual(authenticated, [0,1,1], 'Cancel must not authenticate; Connect Once and Accept must each authenticate once');
    console.log('PASS: themed dialogs, About links, defaults, keyboard cancellation, nested details, long Unicode text and SSH trust persistence.');
  } finally {
    for (const client of clients) client.destroy();
    for (const server of servers) server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

// Dialog controls, keyboard messages, and real isolated SSH host trust decisions.
const { Server, utils } = require('ssh2');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { parseArgs } = require('node:util');
const attachSftp = require('./sftp-fixture.cjs');
const { values } = parseArgs({ options: {
  binaries: { type: 'string', default: 'build/native' }, wine: { type: 'string' },
} });
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `message-test-${Date.now()}`);
fs.mkdirSync(artifact);
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.resolve(root, values.binaries, 'ui-smoke-tests.exe'), executable);
const remote = path.join(artifact, 'remote');
fs.mkdirSync(remote); fs.writeFileSync(path.join(remote, 'listed.txt'), 'fixture');
const clients = new Set(), servers = [], authenticated = [0,0,0];
const shells = [0,0,0], files = {};
let refused = 0;
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
          if (index === 2 && !refused) session.on('sftp', (accept, reject) => { ++refused; reject(); });
          else if (index !== 0) attachSftp(session, remote, files);
          session.on('env', accept => { if (accept) accept(); });
          session.on('window-change', accept => { if (accept) accept(); });
          session.on('shell', accept => {
            const stream = accept(); stream.on('error', () => {}); stream.write('SSH dialog fixture\r\n');
            ++shells[index];
            if (index === 1 && shells[index] === 2) fs.writeFileSync(path.join(artifact, 'dialog-second-shell.ready'), 'connected\n');
            if (index === 2) stream.on('data', bytes => {
              if (bytes.includes('x')) fs.writeFileSync(path.join(artifact, 'dialog-existing-input.ready'), 'received\n');
            });
            fs.writeFileSync(path.join(artifact, `dialog-finished-${index}.ready`), 'connected\n');
          });
        }));
      });
      servers.push(server); await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
      env[`WOOK_DIALOG_PORT_${index}`] = String(server.address().port);
    }
    const child = spawn(values.wine || executable, values.wine ? [executable] : [], { cwd: artifact, stdio: 'ignore', env });
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; child.kill(); }, 180000);
    const code = await new Promise((resolve, reject) => { child.on('close', resolve); child.on('error', reject); }).finally(() => clearTimeout(timer));
    assert.ok(!timedOut, 'Timed out waiting for the workspace dialog tests in ' + artifact);
    const report = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    console.log(JSON.stringify({ artifact, runtime: values.wine ? 'Wine' : 'Windows', code, authenticated, shells, ...report }, null, 2));
    assert.equal(code, 0); assert.equal(report.passed, true, JSON.stringify(report));
    assert.deepEqual(authenticated, [0,3,3], 'Only approved connections may authenticate');
    assert.deepEqual(shells, [0,2,1]);
    assert.equal(refused, 1);
    assert.equal(files.connections, 2, 'Both the approved SFTP session and the retry must open a file channel');
    console.log('PASS: themed dialogs, SSH trust choices, simultaneous SSH/SFTP prompts, existing shell input and SFTP retry.');
  } finally {
    for (const client of clients) client.end();
    for (const server of servers) server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

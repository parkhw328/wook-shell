// Exercise the release EXE's public arguments against an isolated SSH server.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `launch-test-${Date.now()}`);
const data = path.join(artifact, 'data');
fs.mkdirSync(artifact, { recursive: true });
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/wShell.exe'), executable);
const key = utils.generateKeyPairSync('ed25519').private;
const password = 'fixture 한글 & | " \\ ' + crypto.randomBytes(16).toString('hex');
const clients = new Set(), processes = new Set();
const watchdog = setTimeout(() => { for (const child of processes) child.kill(); process.exitCode = 1; server.close(); }, 60000);
let authenticated = 0, shells = 0;
const server = new Server({ hostKeys: [key] }, client => {
  clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
  client.on('authentication', ctx => {
    if (ctx.username === 'seed' && ctx.method === 'none') ctx.accept();
    else if (ctx.username === 'deploy' && ctx.method === 'password' && ctx.password === password) {
      ++authenticated; ctx.accept();
    } else ctx.reject(['password']);
  });
  client.on('ready', () => client.on('session', accept => {
    const session = accept();
    session.on('pty', accept => accept());
    session.on('env', accept => { if (accept) accept(); });
    session.on('exec', accept => { const stream = accept(); stream.exit(0); stream.end(); });
    session.on('shell', accept => { ++shells; const stream = accept(); stream.on('error', () => {}); stream.write('CLI fixture connected\r\n'); });
  }));
});
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate) {
  const limit = Date.now() + 15000;
  while (!predicate()) { if (Date.now() > limit) throw new Error('Timed out waiting for CLI SSH authentication'); await delay(50); }
}
function start(file, args, input = '') {
  const child = spawn(file, args, { cwd: artifact, windowsHide: true, env: { ...process.env, WOOK_DATA_DIR: data }, stdio: ['pipe', 'ignore', 'ignore'] });
  processes.add(child); child.on('exit', () => processes.delete(child));
  child.stdin.on('error', () => {}); child.stdin.end(input); return child;
}
async function childArguments(pid) {
  const command = `Get-CimInstance Win32_Process -Filter 'ParentProcessId = ${pid}' | Select-Object -ExpandProperty CommandLine | ConvertTo-Json -Compress`;
  return new Promise((resolve, reject) => {
    const probe = spawn('powershell.exe', ['-NoProfile', '-Command', command], { windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'] });
    let output = ''; probe.stdout.on('data', chunk => output += chunk);
    probe.once('error', reject);
    probe.once('exit', code => code === 0 ? resolve(output) : reject(new Error('Could not inspect terminal process arguments')));
  });
}
async function stop(child) {
  if (child.exitCode !== null || child.signalCode !== null) return;
  const exited = new Promise(resolve => child.once('exit', resolve)); child.kill(); await exited;
}
function records() {
  return fs.readdirSync(path.join(data, 'sessions')).filter(name => name.endsWith('.ws')).map(name => {
    const text = fs.readFileSync(path.join(data, 'sessions', name), 'utf8');
    assert.ok(!text.includes(password) && !text.includes(Buffer.from(password).toString('hex')), 'No plaintext password in settings');
    return { name: Buffer.from(name.slice(0, -3), 'hex').toString('utf8'), file: name, text };
  });
}
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = String(server.address().port);
  // Trust only this generated loopback fixture, using the normal engine UI.
  const seed = start(path.join(root, 'build/native/plink.exe'), ['-legacy-stdio-prompts', '-no-antispoof', '-ssh', '-P', port, '-l', 'seed', '127.0.0.1', 'seed'], 'y\n');
  const seedCode = await new Promise(resolve => seed.once('exit', resolve));
  assert.equal(seedCode, 0, 'Fixture trust setup succeeds');
  const launch = async (args, input = '') => {
    const expected = shells + 1;
    const child = start(executable, args, input);
    await until(() => shells === expected);
    const commandLines = await childArguments(child.pid);
    assert.ok(commandLines.includes('--terminal') && !commandLines.includes('-pw') && !commandLines.includes('--password') &&
              !commandLines.includes(password.slice(-32)), 'Terminal child arguments contain only the saved session reference');
    const stores = records();
    assert.ok(!stores.some(r => r.name.startsWith('__wook_launch_')), 'One-use ciphertext is removed after consumption');
    const saved = stores.filter(r => r.name !== 'Default Settings' && !r.name.startsWith('__wook_'));
    assert.equal(saved.length, 1, 'Repeating the endpoint reuses its host');
    assert.equal(saved[0].name, 'CLI 서울 host');
    const fields = Object.fromEntries(saved[0].text.trim().split(/\r?\n/).slice(1).map(line => line.split('=').map(hex => Buffer.from(hex, 'hex').toString('utf8'))));
    assert.ok(!fields.WookSshPasswordDPAPI, 'Launch password is not persisted on the saved host');
    await stop(child); return saved[0];
  };
  const original = await launch(['-ssh', 'deploy@127.0.0.1', '-P', port, '--name', 'CLI 서울 host', '-pw', password]);
  const repeated = await launch(['--host', '127.0.0.1', '--user', 'deploy', '--port', port, '--password', password]);
  assert.equal(repeated.text, original.text, 'Reusing a saved host leaves its settings byte-for-byte unchanged');
  const piped = await launch(['127.0.0.1', '-l', 'deploy', '-P', port, '--password-stdin'], password + '\r\n');
  assert.equal(piped.text, original.text, 'Password pipe does not change the saved host');
  assert.equal(authenticated, 3, 'All public launch forms authenticate exactly once');
  const report = { passed: true, authenticated, shells, puttyArguments: true, longOptions: true, utf8Pipe: true, passwordSingleUse: true, savedHostPreserved: true, childArgumentsContainNoPassword: true };
  fs.writeFileSync(path.join(artifact, 'result.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report)); console.log('Evidence: ' + artifact);
})().catch(error => { console.error(error.message); process.exitCode = 1; }).finally(async () => {
  for (const child of processes) await stop(child);
  for (const client of clients) client.end(); server.close(); clearTimeout(watchdog);
});

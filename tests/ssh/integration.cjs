// Real SSH transport against an isolated loopback-only fixture; no external host.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');
const assert = require('node:assert/strict');
const { StringDecoder } = require('node:string_decoder');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', 'ssh-test-' + Date.now());
fs.mkdirSync(artifact, { recursive: true });
const data = path.join(artifact, 'data');
const binaries = path.join(root, 'build', 'native');
const standalone = path.join(artifact, 'standalone');
fs.mkdirSync(standalone);
const sftpRoot=path.join(artifact,'remote');fs.mkdirSync(sftpRoot);
fs.writeFileSync(path.join(sftpRoot,'안녕하세요.txt'),'Remote UTF-8 sample\n');fs.mkdirSync(path.join(sftpRoot,'projects'));
fs.writeFileSync(path.join(sftpRoot,'.env'),'hidden fixture\n');fs.mkdirSync(path.join(sftpRoot,'.config'));
const importKey = path.join(artifact, 'import-키.openssh');
fs.writeFileSync(importKey, utils.generateKeyPairSync('ed25519', {
  passphrase: 'fixture-passphrase', cipher: 'aes256-cbc',
}).private);
const password = crypto.randomBytes(24).toString('hex');
const passwordFile = path.join(artifact, 'test-password.txt');
fs.writeFileSync(passwordFile, password + '\n');
const key = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem' });
const fingerprint = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/, '');
const events = { authenticated: [], publicKeySignatures: 0, passwords: [], methods: [], shells: 0, terminalTypes: [], environment: [], resizes: 0, received: '' };
const clients = new Set();
const syncInput = {};
let passwordShells = 0;
const signal = name => fs.writeFileSync(path.join(standalone, name + '.ready'), 'ready\n');
const server = new Server({ hostKeys: [key] }, client => {
  let username = '';
  clients.add(client);
  client.on('error', () => {});
  client.on('close', () => clients.delete(client));
  client.on('authentication', ctx => {
    username = ctx.username;
    events.methods.push({ user: ctx.username, method: ctx.method });
    if ((ctx.username === 'none' || ctx.username === 'shell' || ctx.username.startsWith('sync-')) && ctx.method === 'none') ctx.accept();
    else if ((ctx.username === 'password' || ctx.username === 'password-fallback') && ctx.method === 'password') {
      const accepted = ctx.password === password;
      events.passwords.push({ user: ctx.username, accepted });
      if (accepted) ctx.accept(); else { ctx.reject(['password']); signal('password-rejected'); }
    }
    else if (ctx.username === 'key' && ctx.method === 'publickey') {
      const allowed = utils.parseKey(fs.readFileSync(path.join(standalone, 'client-key.pub')));
      if (ctx.key.data.equals(allowed.getPublicSSH()) &&
          (!ctx.signature || allowed.verify(ctx.blob, ctx.signature, ctx.hashAlgo) === true)) {
        if (ctx.signature) ++events.publicKeySignatures;
        ctx.accept();
      } else ctx.reject(['publickey']);
    } else ctx.reject(['publickey', 'password']);
  });
  client.on('ready', () => {
    events.authenticated.push(true);
    client.on('session', accept => {
      const session = accept();
      require('./sftp-fixture.cjs')(session,sftpRoot);
      session.on('pty', (accept, reject, info) => { events.terminalTypes.push(info.term); accept(); });
      session.on('env', (accept, reject, info) => { events.environment.push(info); if (accept) accept(); });
      session.on('window-change', (accept) => { ++events.resizes; signal('pty-resize'); if (accept) accept(); });
      session.on('exec', (accept, reject, info) => {
        const stream = accept();
        stream.write('WOOK_SSH_OK ' + info.command + '\n');
        stream.exit(0); stream.end();
      });
      session.on('shell', accept => {
        ++events.shells;
        const stream = accept();
        if (username.startsWith('sync-')) {
          syncInput[username] = '';
          stream.write(username === 'sync-application' ? '\x1b[?1h' : '\x1b[?1l');
          signal(username);
          const decoder = new StringDecoder('utf8');
          stream.on('data', bytes => {
            syncInput[username] += decoder.write(bytes);
            if (syncInput[username].includes('paste-한글')) signal('sync-input-' + username.slice(5));
          });
          stream.on('error', () => {});
          return;
        }
        stream.write('\x1b[2J\x1b[H\r\n  \x1b[38;2;58;169;159mSSH CONNECTED\x1b[0m  /  encrypted loopback test\r\n\r\n');
        stream.write('  PuTTY engine + wShell workspace\r\n  UTF-8: 한글 서버 / 日本語 / café\r\n\r\n');
        stream.write('  \x1b[31mANSI\x1b[0m   \x1b[38;5;172m256 colors\x1b[0m   \x1b[38;2;139;126;200m24-bit color\x1b[0m\r\n\r\n');
        stream.write('  developer@loopback  $ ');
        if (username === 'password') signal('password-shell-' + ++passwordShells);
        if (username === 'password-fallback') signal('password-fallback-shell');
        if (username === 'key') signal('key-shell');
        const decoder = new StringDecoder('utf8');
        stream.on('data', bytes => {
          events.received += decoder.write(bytes); stream.write(bytes);
          if (username === 'password') for (const marker of ['y', 'z']) {
            if (bytes.includes(marker)) signal('password-input-' + marker);
          }
        });
        stream.on('error', () => {});
      });
    });
  });
});
function run(executable, args, input = '', environment = {}) {
  console.log('Running ' + path.basename(executable) + ' : ' + args.at(-1));
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, {
      cwd: path.dirname(executable), windowsHide: !environment.WOOK_TEST_PORT,
      env: { ...process.env, WOOK_DATA_DIR: data, ...environment },
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    let stdout = '', stderr = '';
    const timer = setTimeout(() => { child.kill(); reject(new Error('Timed out: ' + path.basename(executable) + '\n' + stdout + '\n' + stderr)); }, 180000);
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
  assert.ok(fs.existsSync(path.join(data, 'trust')), 'Host trust must persist in the isolated settings folder');
  assert.equal(registryDigest(), before, 'Existing PuTTY registry must not change');

  const ui = path.join(standalone, 'wShell.exe');
  fs.copyFileSync(path.join(binaries, 'ui-smoke-tests.exe'), ui);
  assert.deepEqual(fs.readdirSync(standalone), ['wShell.exe'], 'Start from a folder containing only the executable');
  result = await run(ui, [], '', { WOOK_TEST_PORT: String(port), WOOK_TEST_FINGERPRINT: fingerprint, WOOK_TEST_IMPORT_KEY: importKey, WOOK_TEST_PASSWORD_FILE: passwordFile });
  assert.ok(fs.existsSync(path.join(standalone, 'ui-smoke-result.json')), 'UI exited without a report; code=' + result.code + '; progress=' + fs.readFileSync(path.join(standalone, 'ui-smoke-progress.json'), 'utf8'));
  const report = JSON.parse(fs.readFileSync(path.join(standalone, 'ui-smoke-result.json'), 'utf8'));
  assert.equal(fs.readdirSync(standalone).filter(n => n.toLowerCase().endsWith('.exe')).length, 1, 'Do not extract helper executables');
  assert.ok(!fs.existsSync(path.join(standalone, 'fonts')) && !fs.existsSync(path.join(standalone, 'assets')), 'Fonts and branding stay embedded');
  assert.equal(report.passed, true, JSON.stringify(report));
  const settings = ['settings-connect.json', 'settings-authorities.json', 'settings-live-1.json', 'settings-live-2.json', 'settings-live-3.json'].map(file => {
    const result = JSON.parse(fs.readFileSync(path.join(standalone, file), 'utf8'));
    assert.equal(result.passed, true, file + ': ' + JSON.stringify(result)); return result;
  });
  assert.ok(events.shells >= 1, 'GUI terminal must authenticate and open a real SSH shell');
  assert.ok(events.publicKeySignatures >= 1, 'The independently verified SSH signature must use a key generated inside wShell');
  assert.ok(events.terminalTypes.includes('xterm-256color'), 'PTY terminal type must support color');
  assert.ok(events.environment.some(e => e.key === 'COLORTERM' && e.val === 'truecolor'), 'True Color environment must be requested');
  assert.match(events.received, /x/, 'Keyboard data must cross the real SSH connection');
  assert.equal(events.received.split('|ime:한글🙂|').length - 1, 1, 'IME Unicode must cross SSH exactly once without mojibake');
  assert.ok(events.resizes >= 1, 'PTY resize must reach the server');
  assert.equal(registryDigest(), before, 'GUI must leave existing PuTTY registry unchanged');
  fs.writeFileSync(path.join(artifact, 'authentication.json'), JSON.stringify({ passwords: events.passwords, methods: events.methods, shells: events.shells }, null, 2));
  assert.equal(events.passwords.filter(p => p.user === 'password' && p.accepted).length, 5, 'Encrypted password must authenticate the GUI, duplicate, reconnect and SFTP, plus the CLI baseline; attempts=' + JSON.stringify(events.passwords));
  assert.deepEqual(fs.readFileSync(path.join(standalone,'sftp-download','upload-한글.txt')),fs.readFileSync(path.join(standalone,'sftp-local','upload-한글.txt')),'GUI SFTP round trip must preserve exact bytes');
  assert.deepEqual(events.passwords.filter(p => p.user === 'password-fallback').map(p => p.accepted), [false, true], 'Rejected saved password must be tried only once, then allow manual input');
  assert.match(events.received, /y.*z/s, 'Password sessions must open usable shells before and after reconnect');
  const tail = '\x1b[1~\x1b[4~\x1b[3~\x7f\t\x1b\r\x03paste-한글';
  assert.equal(syncInput['sync-normal'], 'x\x1b[A\x1b[B\x1b[C\x1b[D' + tail, 'Normal cursor mode must receive exact input once');
  assert.equal(syncInput['sync-application'], 'x\x1bOA\x1bOB\x1bOC\x1bOD' + tail, 'Application cursor mode must translate navigation independently');
  assert.equal(syncInput['sync-excluded'], '', 'Excluded SSH pane must receive no input');
  assert.equal(syncInput['sync-hidden'], '', 'Hidden SSH tab must receive no input');
  const summary = { passed: true, sshChecks: 20, ui: report, settings, events, syncInput };
  fs.writeFileSync(path.join(artifact, 'result.json'), JSON.stringify(summary, null, 2));
  console.log(JSON.stringify(summary, null, 2));
  console.log('Evidence: ' + artifact);
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => {
  for (const client of clients) client.end();
  server.close();
  fs.unlinkSync(passwordFile);
  fs.unlinkSync(importKey);
});

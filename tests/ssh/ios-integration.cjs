// Simulator-only loopback fixture; no user keys, Apple account or real servers.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const evidence = path.join(root, 'build/ipad-evidence'); fs.mkdirSync(evidence, { recursive:true });
const scratch = fs.mkdtempSync(path.join(root, 'build/ipad-fixture-'));
const remote = path.join(scratch, 'remote'); fs.mkdirSync(remote);
fs.writeFileSync(path.join(remote, '안녕하세요.txt'), 'wShell SFTP fixture\n');
fs.mkdirSync(path.join(remote, 'projects'));
const passphrase = crypto.randomBytes(18).toString('hex');
const rsa = crypto.generateKeyPairSync('rsa', { modulusLength:2048,
  publicKeyEncoding:{type:'spki', format:'pem'}, privateKeyEncoding:{type:'pkcs8', format:'pem', cipher:'aes-256-cbc', passphrase} });
const ed25519 = utils.generateKeyPairSync('ed25519');
const serverKey = utils.generateKeyPairSync('ed25519');
const rsaPublic = utils.parseKey(crypto.createPrivateKey({ key:rsa.privateKey, passphrase }).export({type:'pkcs1', format:'pem'}));
const edPublic = utils.parseKey(ed25519.private);
const host = utils.parseKey(serverKey.private);
const password = crypto.randomBytes(24).toString('hex');
const events = {passwords:0, rsa:0, ed25519:0, shells:0, resizes:0, input:'', algorithms:[]};
const clients = new Set();
const server = new Server({hostKeys:[serverKey.private]}, client => {
  clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
  client.on('authentication', ctx => {
    if (ctx.username === 'password' && ctx.method === 'password' && ctx.password === password) {
      ++events.passwords; return ctx.accept();
    }
    const key = ctx.username === 'rsa' ? rsaPublic : ctx.username === 'ed25519' ? edPublic : null;
    if (key && ctx.method === 'publickey' && ctx.key.data.equals(key.getPublicSSH()) &&
        (!ctx.signature || key.verify(ctx.blob, ctx.signature, ctx.hashAlgo) === true)) {
      if (ctx.signature) { ++events[ctx.username]; events.algorithms.push(ctx.key.algo); }
      return ctx.accept();
    }
    ctx.reject(['publickey','password']);
  });
  client.on('ready', () => client.on('session', accept => {
    const session = accept(); require('./sftp-fixture.cjs')(session, remote, events);
    session.on('pty', (accept, reject, info) => { assert.equal(info.term, 'xterm-256color'); accept(); });
    session.on('window-change', accept => { ++events.resizes; accept?.(); });
    session.on('shell', accept => {
      ++events.shells; const stream = accept();
      stream.write('\r\n\x1b[38;2;218;112;44mwShell for iPad\x1b[0m\r\nWSHELL_SSH_READY\r\n$ ');
      const decoder = new (require('node:string_decoder').StringDecoder)('utf8');
      stream.on('data', data => { events.input += decoder.write(data); stream.write(data); });
    });
  }));
});
const sim = (...args) => execFileSync('xcrun', ['simctl', ...args], {encoding:'utf8'}).trim();
let device;
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const runtimes = JSON.parse(sim('list','runtimes','--json')).runtimes.filter(r => r.isAvailable && r.identifier.includes('iOS'));
    assert(runtimes.length, 'An iOS simulator runtime is required');
    const type = JSON.parse(sim('list','devicetypes','--json')).devicetypes.find(d => d.name.includes('iPad Pro 13'));
    assert(type, 'iPad simulator device type');
    device = sim('create', 'wShell isolated tests', type.identifier, runtimes.at(-1).identifier);
    sim('boot', device); sim('bootstatus', device, '-b');
    sim('install', device, path.join(root, 'build/ios/DerivedData/Build/Products/Release-iphonesimulator/wShell.app'));
    const container = sim('get_app_container', device, 'com.wshell.ipad', 'data');
    const documents = path.join(container, 'Documents'); fs.mkdirSync(documents, {recursive:true});
    fs.writeFileSync(path.join(documents, 'smoke.json'), JSON.stringify({port:server.address().port,password,
      rsa:rsa.privateKey, ed25519:ed25519.private, passphrase,
      fingerprint:'SHA256:'+crypto.createHash('sha256').update(host.getPublicSSH()).digest('base64').replace(/=+$/,'')}));
    sim('launch', device, 'com.wshell.ipad', '--smoke-test');
    const resultFile = path.join(documents,'result.json');
    const deadline = Date.now() + 180000;
    while (!fs.existsSync(resultFile) && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 250));
    assert(fs.existsSync(resultFile), 'Simulator test timed out');
    const result = JSON.parse(fs.readFileSync(resultFile));
    fs.copyFileSync(resultFile, path.join(evidence, 'result.json'));
    if (fs.existsSync(path.join(documents, 'terminal.png'))) fs.copyFileSync(path.join(documents, 'terminal.png'), path.join(evidence,'terminal.png'));
    sim('io', device, 'screenshot', path.join(evidence,'sftp.png'));
    assert(result.passed, result.error);
    assert.equal(events.passwords, 1, 'Rejecting host identity must not send a password');
    assert.equal(events.rsa, 2); assert.equal(events.ed25519, 1);
    assert.equal(events.algorithms.filter(x => x === 'rsa-sha2-512' || x === 'rsa-sha2-256').length, 2);
    assert.equal(events.shells, 3); assert(events.resizes >= 3);
    assert.equal(events.input.split('|ime:한글🙂|').length - 1, 3);
    assert(!events.input.includes('취소'));
    const payload = Buffer.from(Array.from({length:1_200_003}, (_, i) => i % 251));
    assert.deepEqual(fs.readFileSync(path.join(remote, 'upload-한글.bin')), payload);
    fs.writeFileSync(path.join(evidence,'server-result.json'), JSON.stringify({passed:true, events}, null, 2));
    console.log(JSON.stringify({passed:true, checks:result.checks, events}));
  } catch (error) {
    // Preserve launcher/securityd evidence before deleting our isolated simulator.
    if (device) {
      try {
        const log = sim('spawn', device, 'log', 'show', '--last', '5m', '--style', 'compact',
          '--predicate', 'eventMessage CONTAINS "com.wshell.ipad" OR process == "wShell"');
        fs.writeFileSync(path.join(evidence, 'simulator.log'), log);
      } catch {}
    }
    throw error;
  } finally {
    for (const client of clients) client.destroy(); server.close();
    if (device) { try { sim('shutdown', device); } catch {} try { sim('delete', device); } catch {} }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

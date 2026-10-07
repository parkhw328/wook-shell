// Import independent key formats and verify real SSH signatures on loopback.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { spawn, spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', 'key-import-' + Date.now());
const data = path.join(artifact, 'data');
fs.mkdirSync(artifact, { recursive: true });
const passphrase = 'fixture-passphrase';
const rsa = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey;
const fixtures = [
    rsa.export({ type: 'pkcs1', format: 'pem' }),
    crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem', cipher: 'aes-128-cbc', passphrase }),
    utils.generateKeyPairSync('ed25519', { passphrase, cipher: 'aes256-cbc' }).private,
];
const names = ['oracle-ssh-key.key', 'encrypted-rsa.pem', 'encrypted-ed25519.openssh'];
const keys = fixtures.map((file, i) => utils.parseKey(file, i ? passphrase : undefined));
names.forEach((name, i) => fs.writeFileSync(path.join(artifact, name), fixtures[i]));
fs.writeFileSync(path.join(artifact, 'public-only.key'), 'ssh-rsa ' + keys[0].getPublicSSH().toString('base64') + ' public-only\n');
fs.writeFileSync(path.join(artifact, 'malformed.key'), 'invalid private key fixture\n');
const originalHashes = fixtures.map(file => crypto.createHash('sha256').update(file).digest('hex'));
const host = utils.generateKeyPairSync('ed25519').private;
const pin = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(host).getPublicSSH()).digest('base64').replace(/=+$/, '');
const clients = new Set(), children = new Set(), signatures = [];
let onOracle = null;
const server = new Server({ hostKeys: [host] }, client => {
    clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
    client.on('authentication', ctx => {
        const index = ctx.username === 'oracle' ? 0 : Number(ctx.username.replace('key-', ''));
        const key = keys[index];
        if (ctx.method !== 'publickey' || !key || !ctx.key.data.equals(key.getPublicSSH()) ||
            (ctx.signature && key.verify(ctx.blob, ctx.signature, ctx.hashAlgo) !== true)) return ctx.reject(['publickey']);
        if (ctx.signature) signatures.push(ctx.username);
        ctx.accept();
    });
    client.on('ready', () => client.on('session', accept => {
        const session = accept();
        session.on('pty', accept => accept());
        session.on('env', accept => { if (accept) accept(); });
        session.on('shell', accept => { const stream = accept(); stream.on('error', () => {}); stream.write('KEY_LOGIN_OK\r\n'); if (onOracle) onOracle(); });
        session.on('exec', accept => { const stream = accept(); stream.write('KEY_SIGNATURE_OK\n'); stream.exit(0); stream.end(); });
    }));
});
function run(file, args, input = '', extra = {}) {
    return new Promise((resolve, reject) => {
        const child = spawn(file, args, { cwd: artifact, windowsHide: true,
            env: { ...process.env, WOOK_DATA_DIR: data, ...extra }, stdio: ['pipe', 'pipe', 'pipe'] });
        children.add(child);
        let stdout = '', stderr = '';
        const timer = setTimeout(() => { child.kill(); reject(new Error('Key test process timed out')); }, 30000);
        child.stdout.on('data', chunk => stdout += chunk); child.stderr.on('data', chunk => stderr += chunk);
        child.stdin.on('error', () => {}); child.stdin.end(input);
        child.on('error', error => { clearTimeout(timer); reject(error); });
        child.on('close', code => { clearTimeout(timer); children.delete(child); resolve({ code, stdout, stderr }); });
    });
}
async function releaseLogin(port) {
    await new Promise((resolve, reject) => {
        const child = spawn(path.join(root, 'build/native/wShell.exe'), ['-ssh', '-l', 'oracle', '-P', String(port), '127.0.0.1'], {
            cwd: artifact, windowsHide: true, env: { ...process.env, WOOK_DATA_DIR: data }, stdio: 'ignore',
        });
        children.add(child);
        let connected = false;
        const timer = setTimeout(() => { child.kill(); reject(new Error('Release app did not authenticate with the imported key')); }, 20000);
        onOracle = () => { connected = true; onOracle = null; child.kill(); };
        child.on('error', error => { clearTimeout(timer); reject(error); });
        child.on('close', () => { children.delete(child); clearTimeout(timer); connected ? resolve() : reject(new Error('Release app exited before key authentication')); });
    });
}
(async () => {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const port = server.address().port;
    const probe = path.join(artifact, 'wShell.exe'); fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), probe);
    const extra = { WOOK_TEST_KEY_IMPORT_ONLY: '1', WOOK_TEST_PORT: String(port), WOOK_TEST_FINGERPRINT: pin };
    if (process.argv[2] === '--inspect-key') extra.WOOK_TEST_INSPECT_KEY = path.resolve(process.argv[3]);
    const result = await run(probe, [], '', extra);
    const report = JSON.parse(fs.readFileSync(path.join(artifact, 'key-import-result.json'), 'utf8'));
    assert.equal(result.code, 0, JSON.stringify(report)); assert.equal(report.passed, true);
    for (let i = 0; i < 3; ++i) {
        const converted = fs.readFileSync(path.join(artifact, `converted-${i}.path`), 'utf8');
        const login = await run(path.join(root, 'build/native/plink.exe'), [
            '-ssh', '-legacy-stdio-prompts', '-no-antispoof', '-hostkey', pin, '-i', converted,
            '-P', String(port), '-l', `key-${i}`, '127.0.0.1', 'verify-key',
        ], i ? passphrase + '\n' : '');
        assert.equal(login.code, 0, 'Imported key SSH failed: ' + login.stderr);
        assert.match(login.stdout, /KEY_SIGNATURE_OK/);
        assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(artifact, names[i]))).digest('hex'), originalHashes[i], 'Source key must stay unchanged');
        // Protected DACLs allow only the importing user and LocalSystem.
        const acl = spawnSync('powershell', ['-NoProfile', '-Command',
            '$a = Get-Acl -LiteralPath $env:WOOK_TEST_KEY; $u = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value; '
            + 'if (!$a.AreAccessRulesProtected) { exit 2 }; foreach ($r in $a.Access) { '
            + '$s = $r.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value; '
            + 'if ($s -ne $u -and $s -ne "S-1-5-18") { exit 3 } }'],
            { windowsHide: true, env: { ...process.env, WOOK_TEST_KEY: converted } });
        assert.equal(acl.status, 0, 'Private key file ACL must remain user-scoped');
    }
    const count = () => fs.readdirSync(path.join(data, 'keys')).filter(file => file.endsWith('.ppk')).length;
    const initial = count(); await releaseLogin(port);
    assert.equal(count(), initial + 1, 'A saved host with a PEM path must import it on its first connection');
    await releaseLogin(port);
    assert.equal(count(), initial + 1, 'The next connection must reuse the registered key');
    assert.deepEqual(signatures, ['key-0', 'key-1', 'key-2', 'oracle', 'oracle']);
    assert.ok(!fs.readFileSync(path.join(artifact, 'key-backup.wshell')).includes(Buffer.from('Private-Lines:')), 'Settings backup must exclude private key contents');
    assert.ok(!fs.readdirSync(path.join(data, 'keys')).some(file => file.endsWith('.tmp')), 'Key registration must leave no temporary key file');
    fs.writeFileSync(path.join(artifact, 'result.json'), JSON.stringify({ passed: true, ...report, signaturesVerified: signatures.length }, null, 2));
    console.log('PASS: PEM RSA, encrypted PEM RSA and OpenSSH Ed25519 import, protected registration and 5 real SSH signatures. Evidence: ' + artifact);
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => {
    for (const child of children) child.kill(); for (const client of clients) client.end(); server.close();
});

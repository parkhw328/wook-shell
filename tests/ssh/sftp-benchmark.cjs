// Same payload and server delay for before/after runs; no external hosts.
const { Server, utils } = require('ssh2');
const crypto = require('node:crypto'), fs = require('node:fs'), path = require('node:path');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const attach = require('./sftp-fixture.cjs');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', 'sftp-benchmark-' + Date.now());
const remote = path.join(artifact, 'remote');
fs.mkdirSync(remote, { recursive: true });
const payload = crypto.randomBytes(8 * 1024 * 1024 + 3);
fs.writeFileSync(path.join(remote, 'payload.bin'), payload);
const key = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem' });
const pin = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/, '');
const clients = new Set();
let scenario;
const server = new Server({ hostKeys: [key] }, client => {
    clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
    client.on('authentication', ctx => ctx.username === 'none' && ctx.method === 'none' ? ctx.accept() : ctx.reject());
    client.on('ready', () => client.on('session', accept => attach(accept(), remote, scenario)));
});
(async () => {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    for (const readLimit of [32768, 17003]) {
        scenario = { readLimit, readDelay: 40 };
        const local = path.join(artifact, String(readLimit)); fs.mkdirSync(local);
        const child = spawn(path.join(root, 'build/native/sftp-tests.exe'), [], { windowsHide: true, env: {
            ...process.env, WOOK_DATA_DIR: path.join(local, 'data'), WOOK_SFTP_LOCAL: local,
            WOOK_SFTP_ENGINE: path.join(root, 'build/native/wShell.exe'), WOOK_TEST_PORT: String(server.address().port),
            WOOK_TEST_FINGERPRINT: pin, WOOK_SFTP_BENCHMARK: '1',
        } });
        let output = ''; child.stdout.on('data', data => output += data); child.stderr.pipe(process.stderr);
        const timer = setTimeout(() => child.kill(), 120000);
        const code = await new Promise((resolve, reject) => { child.on('error', reject); child.on('close', resolve); });
        clearTimeout(timer); assert.equal(code, 0);
        assert.deepEqual(fs.readFileSync(path.join(local, 'benchmark.bin')), payload);
        const milliseconds = Number(output.match(/Download milliseconds: (\d+)/)[1]);
        console.log(JSON.stringify({ readLimit, readDelay: 40, bytes: payload.length, milliseconds,
            KBps: Number((payload.length / 1024 / (milliseconds / 1000)).toFixed(1)) }));
    }
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => {
    for (const client of clients) client.destroy(); server.close();
});

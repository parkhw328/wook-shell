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
const large = process.argv.includes('--large');
const payloadSize = large ? 1536 * 1024 * 1024 + 3 : 8 * 1024 * 1024 + 3;
// Generate and verify in bounded memory even for the 1.5 GiB regression.
const payloadPath = path.join(remote, 'payload.bin');
const fd = fs.openSync(payloadPath, 'wx');
try {
    for (let offset = 0; offset < payloadSize;) {
        const block = crypto.randomBytes(Math.min(1024 * 1024, payloadSize - offset));
        let written = 0;
        while (written < block.length) written += fs.writeSync(fd, block, written, block.length - written);
        offset += block.length;
    }
} finally { fs.closeSync(fd); }
async function digest(file) {
    const hash = crypto.createHash('sha256');
    for await (const block of fs.createReadStream(file)) hash.update(block);
    return hash.digest('hex');
}
const key = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 }).privateKey.export({ type: 'pkcs1', format: 'pem' });
const pin = 'SHA256:' + crypto.createHash('sha256').update(utils.parseKey(key).getPublicSSH()).digest('base64').replace(/=+$/, '');
const rekey = process.argv.includes('--rekey');
const clients = new Set();
let handshakes = 0;
let scenario;
const server = new Server({ hostKeys: [key] }, client => {
    client.on('handshake', () => { ++handshakes; console.log(JSON.stringify({ event: 'handshake', number: handshakes, time: Date.now() })); });
    clients.add(client); client.on('close', () => clients.delete(client)); client.on('error', () => {});
    client.on('authentication', ctx => ctx.username === 'none' && ctx.method === 'none' ? ctx.accept() : ctx.reject());
    client.on('ready', () => client.on('session', accept => attach(accept(), remote, scenario)));
});
(async () => {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const expectedHash = await digest(payloadPath);
    for (const condition of large ? [{ name: 'large', readLimit: 32768, readDelay: 0 }] : [
        { name: 'full', readLimit: 32768, readDelay: 40 },
        { name: 'short', readLimit: 17003, readDelay: 40 },
        { name: 'delayed-blocks', readLimit: 32768, readDelay: offset => offset % (64 * 32768) === 0 ? 300 : 40 },
    ]) {
        const previousHandshakes = handshakes;
        scenario = { ...condition, writeDelay: large ? 0 : 40 };
        fs.rmSync(path.join(remote, 'benchmark-upload.bin'), { force: true });
        const local = path.join(artifact, condition.name); fs.mkdirSync(local);
        const child = spawn(path.join(root, 'build/native/sftp-tests.exe'), [], { windowsHide: true, env: {
            ...process.env, WOOK_DATA_DIR: path.join(local, 'data'), WOOK_SFTP_LOCAL: local,
            WOOK_SFTP_ENGINE: path.join(root, 'build/native/wShell.exe'), WOOK_TEST_PORT: String(server.address().port),
            WOOK_TEST_FINGERPRINT: pin, WOOK_SFTP_BENCHMARK: '1',
            WOOK_SFTP_REKEY: rekey ? (large ? '256M' : '256K') : '',
        } });
        let output = ''; child.stdout.on('data', data => output += data); child.stderr.pipe(process.stderr);
        const timer = setTimeout(() => child.kill(), large ? 30 * 60 * 1000 : 120000);
        const code = await new Promise((resolve, reject) => { child.on('error', reject); child.on('close', resolve); });
        clearTimeout(timer); console.log(output.trim()); assert.equal(code, 0);
        if (rekey) assert.ok(handshakes - previousHandshakes > 2, 'Repeated key exchanges must complete during transfer');
        assert.ok(scenario.initialWindow >= 2 * 1024 * 1024, 'SFTP must start with a bulk-transfer SSH window');
        for (const file of [path.join(local, 'benchmark.bin'), path.join(remote, 'benchmark-upload.bin')]) {
            assert.equal(fs.statSync(file).size, payloadSize);
            assert.equal(await digest(file), expectedHash);
        }
        const uploadMilliseconds = Number(output.match(/Upload milliseconds: (\d+)/)[1]);
        const milliseconds = Number(output.match(/Download milliseconds: (\d+)/)[1]);
        const longestProgressGapMilliseconds = Number(output.match(/Download longest progress gap milliseconds: (\d+)/)[1]);
        console.log(JSON.stringify({ condition: condition.name, readLimit: condition.readLimit,
            readDelay: typeof condition.readDelay === 'function' ? '40ms; first block of each 2MiB delayed 300ms' : condition.readDelay,
            writeDelay: scenario.writeDelay, initialWindow: scenario.initialWindow, bytes: payloadSize, milliseconds, uploadMilliseconds, longestProgressGapMilliseconds,
            uploadKBps: Number((payloadSize / 1024 / (uploadMilliseconds / 1000)).toFixed(1)),
            KBps: Number((payloadSize / 1024 / (milliseconds / 1000)).toFixed(1)) }));
    }
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(() => {
    for (const client of clients) client.destroy(); server.close();
});

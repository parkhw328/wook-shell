// Validate private registration, cache reuse/repair and concurrent startup.
// The probe exits without creating a window or changing desktop focus.
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `font-test-${Date.now()}`);
fs.mkdirSync(artifact);
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
function probe(data) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, ['--font-probe'], {
      cwd: artifact, windowsHide: true,
      env: { ...process.env, WOOK_DATA_DIR: data }, stdio: 'ignore',
    });
    const timeout = setTimeout(() => child.kill(), 15000);
    child.on('error', error => { clearTimeout(timeout); reject(error); });
    child.on('close', code => { clearTimeout(timeout); code === 0 ? resolve() : reject(new Error(`Font probe exited ${code}`)); });
  });
}
function checkCache(data) {
  const folder = path.join(data, 'fonts');
  const files = ['Regular', 'Bold'].map(weight => {
    const bytes = fs.readFileSync(path.join(root, `assets/fonts/JetBrainsMono-${weight}.ttf`));
    const hash = crypto.createHash('sha256').update(bytes).digest('hex');
    const file = path.join(folder, `JetBrainsMono-${weight}-${hash}.ttf`);
    assert.deepEqual(fs.readFileSync(file), bytes, `${weight} cache must match the embedded source`);
    return file;
  });
  assert.deepEqual(fs.readdirSync(folder).sort(), files.map(file => path.basename(file)).sort(), 'No partial cache files remain');
  return files;
}
(async () => {
  const data = path.join(artifact, 'data');
  await probe(data);
  const files = checkCache(data);
  const times = files.map(file => fs.statSync(file).mtimeMs);
  await probe(data);
  assert.deepEqual(files.map(file => fs.statSync(file).mtimeMs), times, 'Valid cache files must not be rewritten');
  fs.writeFileSync(files[0], Buffer.alloc(fs.statSync(files[0]).size, 0));
  fs.unlinkSync(files[1]);
  await probe(data);
  checkCache(data);
  const concurrent = path.join(artifact, 'concurrent');
  await Promise.all(Array.from({ length: 8 }, () => probe(concurrent)));
  checkCache(concurrent);
  assert.deepEqual(fs.readdirSync(artifact).filter(name => name.endsWith('.exe')), ['wShell.exe']);
  fs.writeFileSync(path.join(artifact, 'result.json'), JSON.stringify({ passed: true, parallelProcesses: 8 }));
  console.log(`PASS: private font registration, cache reuse, corrupt/missing cache repair, 8 concurrent processes (${artifact})`);
})().catch(error => { console.error(error); process.exitCode = 1; });

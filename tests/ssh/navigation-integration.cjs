// Native navigation gestures and hover tooltips, isolated from user data.
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `navigation-test-${Date.now()}`);
fs.mkdirSync(artifact, { recursive: true });
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
(async () => {
  const child = spawn(executable, [], {
    cwd: artifact, windowsHide: false, stdio: 'ignore',
    env: { ...process.env, WOOK_TEST_NAVIGATION_ONLY: '1', WOOK_DATA_DIR: path.join(artifact, 'data') },
  });
  const timer = setTimeout(() => child.kill(), 60000);
  const code = await new Promise((resolve, reject) => {
    child.on('close', resolve); child.on('error', reject);
  }).finally(() => clearTimeout(timer));
  const report = fs.readFileSync(path.join(artifact, 'navigation-result.txt'), 'utf8').trim();
  console.log(JSON.stringify({ artifact, code, report }, null, 2));
  assert.equal(code, 0); assert.match(report, /^PASS \d+ checks$/);
})().catch(error => { console.error(error); process.exitCode = 1; });

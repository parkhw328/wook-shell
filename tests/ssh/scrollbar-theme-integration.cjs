// Run native scrollbar rendering checks on an interactive Windows desktop.
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `scrollbar-theme-test-${Date.now()}`);
fs.mkdirSync(artifact);
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
(async () => {
  const child = spawn(executable, [], {
    cwd: artifact, windowsHide: false, stdio: 'ignore',
    env: { ...process.env, WOOK_TEST_SCROLLBAR_THEME_ONLY: '1' },
  });
  const timer = setTimeout(() => child.kill(), 45000);
  const code = await new Promise((resolve, reject) => { child.on('close', resolve); child.on('error', reject); }).finally(() => clearTimeout(timer));
  const report = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
  console.log(JSON.stringify({ artifact, code, ...report }, null, 2));
  assert.equal(code, 0); assert.equal(report.passed, true, JSON.stringify(report));
})().catch(error => { console.error(error); process.exitCode = 1; });

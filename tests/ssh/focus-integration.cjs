// Fast, isolated regression run for native split focus and cursor rendering.
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const root = path.resolve(__dirname, '../..');
const artifact = path.join(root, 'build', `focus-test-${Date.now()}`);
fs.mkdirSync(artifact);
const executable = path.join(artifact, 'wShell.exe');
fs.copyFileSync(path.join(root, 'build/native/ui-smoke-tests.exe'), executable);
const child = spawn(executable, [], {
  cwd: artifact, windowsHide: false,
  env: { ...process.env, WOOK_TEST_FOCUS_ONLY: '1' }, stdio: 'ignore',
});
const timeout = setTimeout(() => child.kill(), 45000);
child.on('error', error => { clearTimeout(timeout); console.error(error); process.exitCode = 1; });
child.on('close', () => {
  clearTimeout(timeout);
  try {
    const result = JSON.parse(fs.readFileSync(path.join(artifact, 'ui-smoke-result.json'), 'utf8'));
    console.log(JSON.stringify({ artifact, ...result }, null, 2));
    assert.equal(result.passed, true, JSON.stringify(result));
  } catch (error) { console.error(error); process.exitCode = 1; }
});

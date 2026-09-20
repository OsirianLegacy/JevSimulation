import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';

function running(pid) {
  try { process.kill(pid, 0); return true; }
  catch (error) { if (error.code === 'ESRCH') return false; throw error; }
}

test('Node logs survive non-inheritable IDE console handles', { timeout: 10_000 }, async () => {
  const host = spawn(process.env.JEV_LIFECYCLE_HOST, ['--stdio-test'], { windowsHide: true });
  let stdout = '', stderr = '';
  host.stdout.on('data', data => { stdout += data; });
  host.stderr.on('data', data => { stderr += data; });
  const [code] = await once(host, 'close');
  assert.equal(code, 7);
  assert.match(stdout, /NODE_STDOUT_VISIBLE/);
  assert.match(stderr, /NODE_STDERR_VISIBLE/);
});

for (const forced of [false, true]) {
  test(`Node stops on ${forced ? 'forced' : 'normal'} application exit`, { timeout: 10_000 }, async () => {
    const host = spawn(process.env.JEV_LIFECYCLE_HOST, [], { windowsHide: true });
    let pid;
    try {
      const closed = once(host, 'close');
      pid = await new Promise((resolve, reject) => {
        let output = '';
        host.stdout.on('data', data => {
          output += data;
          if (output.includes('\n')) resolve(Number(output.trim()));
        });
        host.once('error', reject);
        host.once('exit', () => reject(new Error('Host exited before reporting Node PID.')));
      });
      assert.ok(Number.isInteger(pid) && pid > 0);
      assert.equal(running(pid), true);
      if (forced) host.kill('SIGKILL');
      else host.stdin.end('\n');
      const [code] = await closed;
      if (!forced) assert.equal(code, 0);
      for (let attempt = 0; attempt < 50 && running(pid); attempt++) await delay(20);
      assert.equal(running(pid), false, 'Node must not outlive its application');
    } finally {
      host.kill('SIGKILL');
      if (pid && running(pid)) process.kill(pid, 'SIGKILL');
    }
  });
}

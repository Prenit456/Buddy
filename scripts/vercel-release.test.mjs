import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile, stat } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const read = path => readFile(new URL('../' + path, import.meta.url), 'utf8');

test('production API routing targets only the current Cloudflare care backend', async () => {
  const config = JSON.parse(await read('vercel.json'));
  assert.equal(config.outputDirectory, 'dist/vercel');
  assert.equal(config.rewrites.find(r => r.source === '/api/v2/:path*')?.destination,
    'https://buddy-remote-test.prenit-kathuria.workers.dev/api/v2/:path*');
  assert.equal(config.rewrites.find(r => r.source === '/api/health')?.destination,
    'https://buddy-remote-test.prenit-kathuria.workers.dev/health');
  assert.ok(!JSON.stringify(config).includes('onrender.com'));
  assert.ok(config.headers.some(h => h.source === '/api/:path*' &&
    h.headers.some(v => v.key === 'Cache-Control' && v.value === 'no-store')));
});

test('Vercel build includes the current portal, username login, settings and hardware controls', async () => {
  execFileSync(process.execPath, ['scripts/build-vercel.mjs'], { cwd: root, stdio: 'pipe' });
  assert.equal(await read('dist/vercel/index.html'), await read('companion-app/portal.html'));
  assert.equal(await read('dist/vercel/settings.html'), await read('companion-app/index.html'));
  for (const path of ['portal.js', 'calls.js', 'session.js', 'app.js', 'care-ui.js',
    'hardware-controls.js', 'device-preview.js', 'portal.css', 'sw.js', 'tft-preview.html']) {
    assert.equal(await read('dist/vercel/' + path), await read('companion-app/' + path));
  }
  const portal = await read('dist/vercel/index.html');
  assert.match(portal, /name="username"/);
  assert.doesNotMatch(portal, /type="email"/);
  assert.match(portal, /device-preview\.js/);
  const worker = await read('dist/vercel/sw.js');
  assert.match(worker, /v12-cloudflare/);
  assert.match(worker, /includes\("\/api\/"\)/);
  await assert.rejects(stat(new URL('../dist/vercel/.env.local', import.meta.url)));
});

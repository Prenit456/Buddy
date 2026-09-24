import { execFileSync } from 'node:child_process';
import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
let tracked;
try {
  tracked = execFileSync('git', ['ls-files', '--cached', '-z'], { cwd: root, stdio: ['ignore', 'pipe', 'ignore'] }).toString().split('\0').filter(Boolean);
} catch {
  console.error('Run git init and git add first, then run npm run check:repo.');
  process.exit(1);
}
const privatePath = /(^|\/)(\.env(?!\.example$)|HubSecrets\.h$|\.pio\/|\.vercel\/)|^care-hub\/state\/(?!\.gitkeep$)|^dist\//;
const unsafe = tracked.filter(path => privatePath.test(path));
let keyFiles = [];
try {
  keyFiles = execFileSync('git', ['grep', '--cached', '-l', '-i', '-E', 'sk-or-v1-|nvapi-|sk_[a-z0-9]{16,}'], { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim().split('\n').filter(Boolean);
} catch (error) {
  if (error.status !== 1) throw error;
}
if (unsafe.length || keyFiles.length) {
  console.error('Do not push yet. Sensitive files are staged:');
  for (const path of [...new Set([...unsafe, ...keyFiles])]) console.error(' - ' + path);
  process.exit(1);
}
console.log(`Checked ${tracked.length} staged files: no known private paths or key patterns.`);

import { cp, copyFile, mkdir, rename, rm } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const source = join(root, 'companion-app');
const output = join(root, 'dist', 'vercel');
await rm(output, { recursive: true, force: true });
await mkdir(dirname(output), { recursive: true });
await cp(source, output, { recursive: true });
await rename(join(output, 'index.html'), join(output, 'settings.html'));
await copyFile(join(output, 'portal.html'), join(output, 'index.html'));
console.log('Built the existing Buddy website for Vercel in dist/vercel');

// Vercel serves the existing website; the persistent Buddy backend stays on
// an always-on host such as the Render service defined in render.yaml.
const rawBackend = process.env.BUDDY_BACKEND_URL?.trim();
if (!rawBackend) {
  throw new Error('Set BUDDY_BACKEND_URL in Vercel to the hosted Buddy backend HTTPS origin before deploying.');
}

let backend;
try { backend = new URL(rawBackend); }
catch { throw new Error('BUDDY_BACKEND_URL must be a valid HTTPS origin.'); }
if (backend.protocol !== 'https:' || backend.username || backend.password ||
    backend.pathname !== '/' || backend.search || backend.hash) {
  throw new Error('BUDDY_BACKEND_URL must be an HTTPS origin only, for example https://buddy-care.onrender.com');
}

export const config = {
  framework: null,
  buildCommand: 'npm run build:vercel',
  outputDirectory: 'dist/vercel',
  rewrites: [
    { source: '/api/:path*', destination: `${backend.origin}/api/:path*` },
    { source: '/settings', destination: '/settings.html' }
  ],
  headers: [
    { source: '/sw.js', headers: [{ key: 'Cache-Control', value: 'no-cache' }] }
  ]
};

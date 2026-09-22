import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { copyFileSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
for (const app of ['gui', 'dashboard']) {
const root = resolve(`../data/webui/${app}`);
copyFileSync(`apps/${app}/index.html`, resolve(root, 'index.html'));
writeFileSync(resolve(root, 'THIRD-PARTY-LICENSES.txt'),
  'Preact\n\n' + readFileSync('node_modules/preact/LICENSE', 'utf8') +
  (app === 'gui' ? '\n\nwebview\n\n' + readFileSync('../third_party/webview/LICENSE', 'utf8') : '\n\nuPlot\n\n' + readFileSync('node_modules/uplot/LICENSE', 'utf8')));
const files = readdirSync(root).filter(name => name !== 'manifest.json').sort().map(path => {
  const bytes = readFileSync(resolve(root, path));
  return { path, size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') };
});
const manifest = {
  schema: 1, source_commit: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
  source_dirty: execFileSync('git', ['status', '--porcelain', '--', '.'], { encoding: 'utf8' }).trim().length > 0,
  node: process.version, npm: process.env.npm_config_user_agent ?? 'unknown', files,
};
writeFileSync(resolve(root, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
}

import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import ts from 'typescript';

async function loadTsModule(relPath, replacements = []) {
  let source = await readFile(new URL(relPath, import.meta.url), 'utf8');
  for (const [pattern, replacement] of replacements) {
    source = source.replace(pattern, replacement);
  }
  const { outputText } = ts.transpileModule(source, {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext },
  });
  return import(`data:text/javascript;base64,${Buffer.from(outputText).toString('base64')}`);
}

const clientSource = await readFile(new URL('../src/api/client.ts', import.meta.url), 'utf8');
const clientOut = ts.transpileModule(clientSource, {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext },
}).outputText;
const clientDataUrl = `data:text/javascript;base64,${Buffer.from(clientOut).toString('base64')}`;

const updateApi = await loadTsModule('../src/api/updateApi.ts', [
  [`from './client'`, `from '${clientDataUrl}'`],
]);

function mockWindow(t, url) {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window');
  Object.defineProperty(globalThis, 'window', { configurable: true, value: { location: new URL(url) } });
  t.after(() => {
    if (previous) Object.defineProperty(globalThis, 'window', previous);
    else delete globalThis.window;
  });
}

test('update API only activates on desktop local backend', async (t) => {
  mockWindow(t, 'https://helper.nixlabs.tech');
  assert.equal(updateApi.isDesktopBackend(), false);
  assert.equal(await updateApi.getUpdateStatus(), null);

  mockWindow(t, 'http://127.0.0.1:45731');
  assert.equal(updateApi.isDesktopBackend(), true);
});

test('update API status, start, and dismiss call local backend endpoints', async (t) => {
  t.mock.method(globalThis, 'fetch', async (url) => {
    if (url.endsWith('/api/update/status')) {
      return Response.json({
        available: true,
        current_version: '1.0.14',
        latest_version: '1.0.15',
        phase: 'available',
        percent: 0,
        download_percent: 0,
        subtitle: '',
        dismissed: false,
      });
    }
    if (url.endsWith('/api/update/start')) {
      return Response.json({
        available: true,
        current_version: '1.0.14',
        latest_version: '1.0.15',
        phase: 'downloading',
        percent: 0,
        download_percent: 0,
        subtitle: 'Downloading 0%',
        dismissed: false,
      });
    }
    if (url.endsWith('/api/update/dismiss')) {
      return Response.json({ available: true, dismissed: true });
    }
    throw new Error(`Unexpected URL: ${url}`);
  });
  mockWindow(t, 'http://127.0.0.1:45731');

  const status = await updateApi.getUpdateStatus();
  assert.equal(status.available, true);
  assert.equal(updateApi.isUpdateModalVisible(status), true);

  const started = await updateApi.startUpdate();
  assert.equal(started.phase, 'downloading');

  const dismissed = await updateApi.dismissUpdate();
  assert.equal(updateApi.isUpdateModalVisible(dismissed), false);
});

test('subtitle and progress semantics match Downloading 0-100% and Installing...', async () => {
  const enUKMod = await loadTsModule('../src/i18n/locales/en-uk.ts');
  const enUK = enUKMod.enUK;
  const t = (key, vars = {}) => {
    return enUK[key].replace(/\{(\w+)\}/g, (_, k) => String(vars[k] ?? `{${k}}`));
  };

  assert.equal(enUK.updateAvailableTitle, 'A new update is available.');
  assert.equal(t('updateVersionLabel', { version: '1.0.15' }), 'Version 1.0.15');
  assert.equal(updateApi.computeUpdateSubtitle('downloading', 0, t), 'Downloading 0%');
  assert.equal(updateApi.computeUpdateSubtitle('downloading', 57, t), 'Downloading 57%');
  assert.equal(updateApi.computeUpdateSubtitle('downloading', 100, t), 'Downloading 100%');
  assert.equal(updateApi.computeUpdateSubtitle('installing', 100, t), 'Installing...');
  assert.equal(updateApi.computeUpdateSubtitle('completed', 100, t), 'Installing...');
});

test('all locale files include update copy keys', async () => {
  const localeFiles = [
    ['en-uk.ts', 'enUK'],
    ['en-us.ts', 'enUS'],
    ['fr-be.ts', 'frBE'],
    ['ja.ts', 'ja'],
    ['pt-br.ts', 'ptBR'],
    ['pt-pt.ts', 'ptPT'],
  ];
  const requiredKeys = [
    'updateAvailableTitle',
    'updateVersionLabel',
    'updateBtn',
    'remindMeLaterBtn',
    'updateDownloading',
    'updateInstalling',
    'doneBtn',
  ];

  for (const [file, exportName] of localeFiles) {
    const mod = await loadTsModule(`../src/i18n/locales/${file}`, [
      [/^import type .*$/m, ''],
    ]);
    const dict = mod[exportName];
    for (const key of requiredKeys) {
      assert.equal(typeof dict[key], 'string', `${file} missing ${key}`);
      assert.ok(dict[key].length > 0, `${file} has empty ${key}`);
    }
  }
});

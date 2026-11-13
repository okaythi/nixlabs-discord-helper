import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import ts from 'typescript';

// Compile the small API module in memory; no browser or build process is needed.
const source = await readFile(new URL('../src/api/client.ts', import.meta.url), 'utf8');
const { outputText } = ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext },
});
const { accountsRequest, apiRequest, getWorkerBaseUrl } = await import(
  `data:text/javascript;base64,${Buffer.from(outputText).toString('base64')}`
);

function mockWindow(t, url) {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window');
  Object.defineProperty(globalThis, 'window', { configurable: true, value: { location: new URL(url) } });
  t.after(() => {
    if (previous) Object.defineProperty(globalThis, 'window', previous);
    else delete globalThis.window;
  });
}

test('desktop login and session requests use the window’s own backend', async (t) => {
  t.mock.method(globalThis, 'fetch', async () => Response.json({ success: true }));
  mockWindow(t, 'http://127.0.0.1:51234');

  await accountsRequest('/api/login', { method: 'POST', body: '{"identifier":"test","password":"test"}' });
  await accountsRequest('/api/session');
  await accountsRequest('/api/logout', { method: 'POST' });
  const calls = fetch.mock.calls;
  assert.deepEqual(calls.map(({ arguments: args }) => args[0]), [
    'http://127.0.0.1:51234/api/accounts/login',
    'http://127.0.0.1:51234/api/accounts/session',
    'http://127.0.0.1:51234/api/accounts/logout',
  ]);
  assert.equal(calls[0].arguments[1].credentials, 'include');
  assert.equal(calls[0].arguments[1].headers.get('Content-Type'), 'application/json');
});

test('desktop account and quest requests share the saved login', async (t) => {
  t.mock.method(globalThis, 'fetch', async () => Response.json({}));
  mockWindow(t, 'http://127.0.0.1:54321');
  await accountsRequest('/api/third-party-auth/discord-helper/bot-profile?refresh=true');
  await apiRequest('/api/quests');
  assert.equal(fetch.mock.calls[0].arguments[0], 'http://127.0.0.1:54321/api/accounts/third-party-auth/discord-helper/bot-profile?refresh=true');
  assert.equal(fetch.mock.calls[1].arguments[0], 'http://127.0.0.1:54321/api/quests');
  assert.equal(getWorkerBaseUrl(), 'http://127.0.0.1:54321');
});

test('hosted app keeps using Accounts and the hosted worker', async (t) => {
  t.mock.method(globalThis, 'fetch', async () => Response.json({ authenticated: false }));
  mockWindow(t, 'https://helper.nixlabs.tech');
  await accountsRequest('/api/session');
  assert.equal(fetch.mock.calls[0].arguments[0], 'https://accounts.nixlabs.tech/api/session');
  assert.equal(getWorkerBaseUrl(), 'https://api-helper.nixlabs.tech');
});

test('incorrect password errors reach the login form', async (t) => {
  t.mock.method(globalThis, 'fetch', async () => Response.json({ error: 'Invalid username or password.' }, { status: 401 }));
  mockWindow(t, 'http://127.0.0.1:51234');
  await assert.rejects(accountsRequest('/api/login', { method: 'POST' }), /Invalid username or password/);
});

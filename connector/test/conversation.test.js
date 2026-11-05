import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, readFileSync, writeFileSync, statSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { ConversationStore } from '../src/conversation.js';
import { PublicError } from '../src/events.js';

function fixture(t, options) {
  const directory = mkdtempSync(join(tmpdir(), 'lynn-conversation-test-'));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  return { directory, store: new ConversationStore(directory, 'lynn', options) };
}
const errorCode = code => error => error instanceof PublicError && error.code === code;

test('one conversation survives restarts with full replies and explicit memories', t => {
  let now = 1000;
  const { directory, store } = fixture(t, { now: () => now });
  const initial = store.begin('request-1', 'My preferred drink is tea.');
  assert.deepEqual(initial.context, []);
  assert.equal(store.resolveRequest(), 'request-1');
  store.remember('favorite_drink', 'Tea');
  assert.equal(store.appendReply('request-1', 'I will remember tea.'), true);
  assert.equal(store.appendReply('request-1', 'Would you like some now?'), true);
  now += 86400000;
  const restarted = new ConversationStore(directory, 'lynn', { now: () => now });
  const second = restarted.begin('request-2', 'What do I drink?');
  assert.equal(second.conversation_id, initial.conversation_id);
  assert.deepEqual(second.context, [{ request_id: 'request-1', user: 'My preferred drink is tea.', assistant: 'I will remember tea.\nWould you like some now?', status: 'replied' }]);
  assert.deepEqual(second.memories, [{ key: 'favorite_drink', text: 'Tea' }]);
  second.context[0].user = 'mutated'; second.memories[0].text = 'mutated';
  assert.equal(restarted.context().context[0].user, 'My preferred drink is tea.');
  assert.equal(restarted.getMemory()[0].text, 'Tea');
  assert.equal(statSync(directory).mode & 0o777, 0o700);
  assert.equal(statSync(join(directory, 'conversation.json')).mode & 0o777, 0o600);
  assert.equal(existsSync(join(directory, 'conversation.json.tmp')), false);
});

test('request correlation rejects ambiguity, unknown and failed turns', t => {
  const { store } = fixture(t);
  assert.equal(store.resolveRequest(), null);
  store.begin('one', 'one'); store.status('one', 'dispatched');
  store.begin('two', 'two'); store.status('two', 'uncertain');
  assert.throws(() => store.resolveRequest(), errorCode('request_id_required'));
  assert.equal(store.resolveRequest('one'), 'one');
  assert.throws(() => store.resolveRequest('unknown'), errorCode('unknown_request'));
  assert.throws(() => store.resolveRequest('bad id'), errorCode('invalid_request_id'));
  store.status('one', 'failed');
  assert.throws(() => store.resolveRequest('one'), errorCode('failed_request'));
  assert.throws(() => store.appendReply('one', 'wrong'), errorCode('failed_request'));
  assert.equal(store.resolveRequest(), 'two');
  store.appendReply('two', 'right');
  assert.equal(store.resolveRequest(), null);
  assert.deepEqual(store.context().context.map(turn => turn.request_id), ['two']);
});

test('expired outstanding requests cannot be silently rebound to a newer request', t => {
  let now = 0;
  const { directory, store } = fixture(t, { now: () => now });
  store.begin('old', 'old'); store.status('old', 'uncertain');
  now = 600000;
  assert.throws(() => store.resolveRequest(), errorCode('expired_request'));
  assert.throws(() => store.appendReply('old', 'late'), errorCode('expired_request'));
  store.begin('new', 'new');
  const restarted = new ConversationStore(directory, 'lynn', { now: () => now });
  assert.throws(() => restarted.resolveRequest(), errorCode('request_id_required'));
  assert.throws(() => restarted.resolveRequest('old'), errorCode('expired_request'));
  assert.equal(restarted.resolveRequest('new'), 'new');
  restarted.appendReply('new', 'new answer');
  now += 600000;
  assert.equal(restarted.appendReply('new', 'another recorded part'), true);
});

test('reply retry dedupe and webhook acceptance races survive restart', t => {
  const { directory, store } = fixture(t);
  store.begin('one', 'hello');
  assert.equal(store.validateReply('one', 'hello back'), true);
  assert.equal(store.context().context[0].status, 'pending');
  assert.equal(store.hasReply('one', 'hello back'), false);
  assert.equal(store.appendReply('one', 'hello back'), true);
  for (const status of ['dispatched', 'uncertain', 'failed']) store.status('one', status);
  const restarted = new ConversationStore(directory, 'lynn');
  assert.equal(restarted.context().context[0].status, 'replied');
  assert.equal(restarted.hasReply('one', 'hello back'), true);
  assert.equal(restarted.replyAlreadyRecorded('one', 'hello back'), true);
  assert.equal(restarted.validateReply('one', 'hello back'), false);
  assert.equal(restarted.appendReply('one', 'hello back'), false);
  assert.equal(restarted.context().context[0].assistant, 'hello back');
});

test('reply part and character bounds reject before changing state', t => {
  const { store } = fixture(t);
  store.begin('parts', 'hello');
  for (let index = 0; index < 8; index++) store.appendReply('parts', `part ${index}`);
  const previous = store.context();
  assert.equal(store.appendReply('parts', 'part 0'), false);
  assert.throws(() => store.validateReply('parts', 'ninth'), errorCode('reply_limit'));
  assert.throws(() => store.appendReply('parts', 'ninth'), errorCode('reply_limit'));
  assert.deepEqual(store.context(), previous);
  store.begin('length', 'hello');
  store.appendReply('length', 'x'.repeat(8000));
  assert.throws(() => store.appendReply('length', 'y'), errorCode('reply_limit'));
  assert.equal(store.context().context.at(-1).assistant.length, 8000);
});

test('bounded recent context archives settled older turns as labeled extracts', t => {
  const { directory, store } = fixture(t);
  for (let index = 0; index < 60; index++) {
    const snapshot = store.begin(`r${index}`, `question ${index}`);
    assert.ok(snapshot.context.length <= 12);
    assert.ok(!snapshot.context.some(turn => turn.request_id === `r${index}`));
    store.appendReply(`r${index}`, `answer ${index}`);
  }
  const state = JSON.parse(readFileSync(join(directory, 'conversation.json'), 'utf8'));
  assert.equal(state.turns.length, 48);
  assert.ok(state.summary.includes('[r0] User: question 0\nAssistant: answer 0'));
  assert.ok(state.summary.includes('[r11] User: question 11\nAssistant: answer 11'));
  assert.ok(state.summary.length <= 4000);
  const restarted = new ConversationStore(directory, 'lynn');
  assert.ok(restarted.context().summary.includes('[r47] User: question 47\nAssistant: answer 47'));
  assert.deepEqual(restarted.context().context.map(turn => turn.request_id), Array.from({ length: 12 }, (_, index) => `r${index + 48}`));
});

test('failed turns are excluded from recent context and archived extracts', t => {
  const { store } = fixture(t);
  store.begin('failed', 'not delivered sensitive context'); store.status('failed', 'failed');
  for (let index = 0; index < 49; index++) {
    store.begin(`r${index}`, `question ${index}`); store.appendReply(`r${index}`, `answer ${index}`);
  }
  assert.ok(!JSON.stringify(store.context()).includes('not delivered sensitive context'));
  assert.ok(!store.context().summary.includes('[failed]'));
});

test('fresh unanswered turns block overflow; expired turns archive with an explicit no-reply marker', t => {
  let now = 0;
  const { directory, store } = fixture(t, { now: () => now });
  for (let index = 0; index < 48; index++) store.begin(`r${index}`, 'unanswered');
  assert.throws(() => store.begin('overflow', 'new'), errorCode('conversation_full'));
  now += 600001;
  store.begin('overflow', 'new');
  const state = JSON.parse(readFileSync(join(directory, 'conversation.json'), 'utf8'));
  assert.ok(state.summary.includes('[r0] Status: expired, no reply'));
  assert.equal(state.turns.length, 48);
  const restarted = new ConversationStore(directory, 'lynn', { now: () => now });
  assert.throws(() => restarted.resolveRequest(), errorCode('request_id_required'));
  assert.throws(() => restarted.resolveRequest('r0'), errorCode('unknown_request'));
  assert.throws(() => restarted.resolveRequest('r1'), errorCode('expired_request'));
  restarted.begin('available', 'new');
  assert.equal(restarted.context().context.at(-1).request_id, 'available');
});

test('explicit memory upserts, searches and bounds persist without implicit extraction', t => {
  const { directory, store } = fixture(t);
  store.begin('one', 'Remember that I like tea'); store.appendReply('one', 'Tea remembered');
  assert.deepEqual(store.getMemory(), []);
  assert.deepEqual(store.remember('favorite_drink', 'Tea'), { key: 'favorite_drink', text: 'Tea' });
  store.remember('favorite_drink', 'Green tea');
  assert.deepEqual(store.getMemory('GREEN'), [{ key: 'favorite_drink', text: 'Green tea' }]);
  assert.equal(store.getMemory('favorite').length, 1);
  for (let index = 1; index < 32; index++) store.remember(`m${index}`, 'x'.repeat(1000));
  assert.throws(() => store.remember('overflow', 'x'), errorCode('memory_full'));
  store.remember('m1', 'updated');
  assert.throws(() => store.remember('../unsafe', 'x'), errorCode('invalid_memory'));
  assert.throws(() => store.remember('too_long', 'x'.repeat(1001)), errorCode('invalid_memory'));
  assert.throws(() => store.remember('empty', ' '), errorCode('invalid_memory'));
  assert.equal(new ConversationStore(directory, 'lynn').getMemory().length, 32);
});

test('webhook projection stays within 20000 characters with large escaped context and memories', t => {
  const { store } = fixture(t);
  for (let index = 0; index < 32; index++) store.remember(`m${index}`, '"'.repeat(1000));
  for (let index = 0; index < 55; index++) {
    store.begin(`r${index}`, '"'.repeat(4000)); store.appendReply(`r${index}`, '\n'.repeat(7999) + 'x');
  }
  const snapshot = store.context();
  assert.ok(JSON.stringify(snapshot).length <= 20000);
  assert.ok(snapshot.summary.length <= 4000);
  assert.equal(snapshot.context.at(-1).request_id, 'r54');
  assert.equal(store.state.turns.at(-1).assistant[0].length, 8000);
  assert.equal(store.getMemory().length, 32);
});

test('corrupt or cross-bot state fails closed without resetting conversation data', t => {
  const { directory, store } = fixture(t);
  store.begin('one', 'hello');
  const file = join(directory, 'conversation.json'), original = readFileSync(file, 'utf8');
  assert.throws(() => new ConversationStore(directory, 'different-bot'), /Invalid conversation state/);
  assert.equal(readFileSync(file, 'utf8'), original);
  const invalid = [
    { ...JSON.parse(original), version: 2 },
    { ...JSON.parse(original), turns: [{ ...store.state.turns[0], status: 'replied' }] },
    { ...JSON.parse(original), turns: [store.state.turns[0], store.state.turns[0]] },
    { ...JSON.parse(original), memories: [{ key: '../bad', text: 'x' }] },
    { ...JSON.parse(original), surprise: true },
  ];
  for (const value of invalid) {
    const encoded = JSON.stringify(value); writeFileSync(file, encoded);
    assert.throws(() => new ConversationStore(directory, 'lynn'), /Invalid conversation/);
    assert.equal(readFileSync(file, 'utf8'), encoded);
  }
  writeFileSync(file, '{broken');
  assert.throws(() => new ConversationStore(directory, 'lynn'), /restore state instead of resetting/);
  assert.equal(readFileSync(file, 'utf8'), '{broken');
  writeFileSync(file, ' '.repeat(2 * 1024 * 1024 + 1));
  assert.throws(() => new ConversationStore(directory, 'lynn'), /restore state instead of resetting/);
});

test('bad requests and invalid status transitions leave no partial turns', t => {
  const { store } = fixture(t);
  assert.throws(() => store.begin('bad id', 'hello'), errorCode('invalid_request_id'));
  assert.throws(() => store.begin('one', ''), errorCode('invalid_conversation_text'));
  assert.throws(() => store.begin('one', 'x'.repeat(4001)), errorCode('invalid_conversation_text'));
  assert.deepEqual(store.context().context, []);
  store.begin('one', 'hello');
  assert.throws(() => store.begin('one', 'duplicate'), errorCode('duplicate_request'));
  assert.throws(() => store.status('one', 'unknown'), errorCode('invalid_conversation_status'));
  assert.throws(() => store.status('one', 'replied'), errorCode('reply_text_required'));
  assert.equal(store.context().context[0].status, 'pending');
  assert.throws(() => store.appendReply('one', ''), errorCode('invalid_reply_text'));
});

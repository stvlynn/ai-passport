import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { EventQueue, splitText } from '../src/events.js';

function fixture(t, options) {
  const directory = mkdtempSync(join(tmpdir(), 'lynn-events-test-'));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  return { directory, queue: new EventQueue(directory, options) };
}

test('text splitting preserves Unicode and the 191-byte firmware limit', () => {
  for (const text of ['Hello '.repeat(150), '你好，Lynn！👩‍🚀'.repeat(90), 'e\u0301'.repeat(250)]) {
    const chunks = splitText(text);
    assert.equal(chunks.join(''), text.normalize('NFC'));
    for (const chunk of chunks) { assert.ok(Buffer.byteLength(chunk) <= 191); assert.ok(!chunk.includes('\ufffd')); }
  }
});

test('initial cursor is nonzero and boot initialization skips historical events', t => {
  const { queue } = fixture(t);
  assert.deepEqual(queue.poll(0), { cursor: 1, events: [] });
  queue.enqueue('action', undefined, 'wave');
  assert.deepEqual(queue.poll(0), { cursor: 2, events: [] });
  const [id] = queue.enqueue('text', 'hello');
  assert.equal(queue.poll(2).events[0].id, id);
});

test('one event per poll, retry replay and animation pacing survive restart', t => {
  let now = 1000;
  const { directory, queue } = fixture(t, { now: () => now });
  queue.enqueue('action', 'hello', 'wave'); queue.enqueue('action', undefined, 'nod');
  const first = queue.poll(1);
  assert.equal(first.events.length, 1); assert.equal(first.events[0].action, 'wave');
  assert.deepEqual(queue.poll(1), first);
  assert.deepEqual(queue.poll(first.cursor), { cursor: first.cursor, events: [] });
  const restarted = new EventQueue(directory, { now: () => now });
  assert.equal(restarted.state.cursor, 3);
  assert.deepEqual(restarted.poll(first.cursor), { cursor: first.cursor, events: [] });
  now += 5000;
  assert.equal(restarted.poll(first.cursor).events[0].action, 'nod');
  assert.ok(Buffer.byteLength(JSON.stringify(first)) < 4096);
});

test('text cards pace, expire and reject queue overflow without allocating IDs', t => {
  let now = 1000;
  const { queue } = fixture(t, { now: () => now, maxEvents: 2, ttlMs: 60000 });
  queue.enqueue('text', 'a'.repeat(192));
  assert.throws(() => queue.enqueue('text', 'overflow'), /event_queue_full/);
  assert.equal(queue.state.cursor, 3);
  assert.equal(queue.poll(1).events[0].text.length, 191);
  assert.deepEqual(queue.poll(2), { cursor: 2, events: [] });
  now += 191 * 250;
  assert.equal(queue.poll(2).events[0].text, 'a');
  now += 60001;
  assert.deepEqual(queue.poll(2), { cursor: 3, events: [] });
  assert.throws(() => queue.poll(4), /invalid_cursor/);
});

test('cursor never wraps uint32', t => {
  const { queue } = fixture(t);
  queue.state.cursor = 0xffffffff;
  assert.throws(() => queue.enqueue('text', 'hello'), /cursor_exhausted/);
});

test('speech cards normalize multiline whitespace for the firmware label', () => {
  assert.deepEqual(splitText('Hello\r\nLynn\t!'), ['Hello Lynn !']);
});

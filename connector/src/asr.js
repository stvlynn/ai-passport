import { spawn } from 'node:child_process';
import { mkdtemp, writeFile, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

export function wav(pcm) {
  const header = Buffer.alloc(44);
  header.write('RIFF'); header.writeUInt32LE(pcm.length + 36, 4); header.write('WAVEfmt ', 8);
  header.writeUInt32LE(16, 16); header.writeUInt16LE(1, 20); header.writeUInt16LE(1, 22);
  header.writeUInt32LE(16000, 24); header.writeUInt32LE(32000, 28); header.writeUInt16LE(2, 32); header.writeUInt16LE(16, 34);
  header.write('data', 36); header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

export async function transcribe(pcm, settings, signal) {
  const directory = await mkdtemp(join(tmpdir(), 'ai-passport-asr-'));
  const input = join(directory, 'audio.wav'), output = join(directory, 'transcript.txt'), outputBase = join(directory, 'transcript');
  try {
    await writeFile(input, wav(pcm), { mode: 0o600 });
    const args = settings.args.map(arg => arg.replaceAll('{input}', input).replaceAll('{outputBase}', outputBase).replaceAll('{output}', output));
    const stdout = []; let bytes = 0;
    await new Promise((resolve, reject) => {
      const child = spawn(settings.command, args, { shell: false, stdio: ['ignore', 'pipe', 'pipe'], signal });
      let settled = false;
      const finish = error => { if (settled) return; settled = true; clearTimeout(timeout); error ? reject(error) : resolve(); };
      const timeout = setTimeout(() => { child.kill('SIGKILL'); finish(new Error('asr_timeout')); }, settings.timeoutMs ?? 120000);
      child.stdout.on('data', data => {
        bytes += data.length;
        if (bytes > 65536) { child.kill('SIGKILL'); finish(new Error('asr_output_limit')); }
        else stdout.push(data);
      });
      // Drain and discard stderr: model paths and transcripts must not enter logs.
      child.stderr.resume();
      child.on('error', () => finish(new Error('asr_failed')));
      child.on('close', code => finish(code === 0 ? null : new Error('asr_failed')));
    });
    let text = Buffer.concat(stdout).toString('utf8');
    if (settings.output !== 'stdout') {
      const { stat } = await import('node:fs/promises');
      if ((await stat(output)).size > 65536) throw new Error('asr_output_limit');
      text = await readFile(output, 'utf8');
    }
    text = text.trim();
    if (!text || text.length > 4000 || /\u0000/.test(text)) throw new Error('asr_invalid_transcript');
    return text;
  } finally { await rm(directory, { recursive: true, force: true }); }
}

export async function dispatchWebhook(config, text, requestId, signal) {
  const response = await fetch(config.webhookURL, {
    method: 'POST', redirect: 'error',
    signal: AbortSignal.any([signal, AbortSignal.timeout(20000)]),
    headers: { Authorization: `Bearer ${config.webhookKey}`, 'Content-Type': 'application/json', 'X-Request-ID': requestId },
    body: JSON.stringify({ text, request_id: requestId, bot_id: config.botId, bot_name: config.botName }),
  });
  await response.body?.cancel();
  // A successful Routine webhook means accepted; it does not contain a bot reply.
  if (response.status !== 200) throw new Error('webhook_failed');
}

import { existsSync } from 'node:fs';
import { loadEnvFile } from 'node:process';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

export const endpoint = 'https://ai-gateway.vercel.sh/v1/evaluate';
export const inquiry = {
  model: 'typesafe-ai/jev',
  state: 'Mira is a town guard. She patrols the north gate. She does not sell goods and is not a merchant.',
  questions: {
    isGuard: { type: 'boolean', instructions: 'Is Mira a town guard?' },
    isMerchant: { type: 'boolean', instructions: 'Is Mira a merchant?' },
  },
};

export function validateResponse(result) {
  if (result?.model !== inquiry.model) throw new Error('Unexpected or missing response model.');
  for (const name of Object.keys(inquiry.questions)) {
    const answer = result?.answers?.[name];
    if (answer?.type !== 'boolean' || !Number.isFinite(answer.probability)
        || answer.probability < 0 || answer.probability > 1) {
      throw new Error(`Invalid boolean probability for ${name}.`);
    }
  }
  // Smoke-test thresholds, not a claim of calibrated model confidence.
  if (result.answers.isGuard.probability < 0.8) {
    throw new Error('Expected isGuard probability >= 0.8.');
  }
  if (result.answers.isMerchant.probability > 0.2) {
    throw new Error('Expected isMerchant probability <= 0.2.');
  }
}

export async function sendInquiry(apiKey, fetchImpl = fetch, timeoutMs = 30_000) {
  if (!apiKey?.trim()) {
    throw new Error('Missing AI_GATEWAY_API_KEY (or API_KEY). Set it in your environment, .env.local, or .env.');
  }
  const response = await fetchImpl(endpoint, {
    method: 'POST',
    redirect: 'error',
    headers: { Authorization: `Bearer ${apiKey.trim()}`, 'Content-Type': 'application/json' },
    body: JSON.stringify(inquiry),
    signal: AbortSignal.timeout(timeoutMs),
  });
  if (!response.ok) {
    const hints = {
      401: 'Check your Vercel AI Gateway key.',
      403: 'Check model access and Gateway permissions.',
      402: 'Check Gateway credits or billing.',
      429: 'Rate limit reached; try again later.',
    };
    // Do not print raw error bodies or request headers, which may contain secrets.
    throw new Error(`HTTP ${response.status}. ${hints[response.status] ?? 'Gateway rejected the inquiry.'}`);
  }
  try {
    return await response.json();
  } catch (error) {
    if (error.name === 'TimeoutError' || error.name === 'AbortError') throw error;
    throw new Error('Gateway returned invalid JSON.');
  }
}

async function main() {
  const args = process.argv.slice(2);
  if (args.length > 1 || (args.length === 1 && args[0] !== '--dry-run')) {
    throw new Error('Usage: node scripts/test-jev.mjs [--dry-run]');
  }
  if (args[0] === '--dry-run') {
    console.log(JSON.stringify({ endpoint, ...inquiry }, null, 2));
    console.log('DRY RUN: no request sent.');
    return;
  }
  for (const name of ['.env.local', '.env']) {
    const envPath = fileURLToPath(new URL(`../${name}`, import.meta.url));
    if (existsSync(envPath)) loadEnvFile(envPath);
  }
  console.log(`Sending inquiry to ${inquiry.model}...`);
  const start = performance.now();
  const result = await sendInquiry(process.env.AI_GATEWAY_API_KEY || process.env.API_KEY);
  // Print only the expected decision fields, never credentials or raw payloads.
  for (const name of Object.keys(inquiry.questions)) {
    const probability = result?.answers?.[name]?.probability;
    console.log(`${name}: ${typeof probability === 'number' ? probability : 'missing/invalid'}`);
  }
  validateResponse(result);
  console.warn(`[WARNING] Jev responded properly: both answers match the known facts (${Math.round(performance.now() - start)} ms).`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => {
    let message = error.name === 'TimeoutError' || error.name === 'AbortError'
      ? 'Request timed out.' : error.message;
    for (const value of [process.env.AI_GATEWAY_API_KEY, process.env.API_KEY]) {
      const key = value?.trim();
      if (key) message = message.replaceAll(key, '[REDACTED]');
    }
    console.error(`[ERROR] Jev test failed: ${message}`);
    process.exitCode = 1;
  });
}

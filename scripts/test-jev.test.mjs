import test from 'node:test';
import assert from 'node:assert/strict';
import { endpoint, inquiry, sendInquiry, validateResponse } from './test-jev.mjs';

const good = () => ({ model: inquiry.model, answers: {
  isGuard: { type: 'boolean', probability: 0.99 },
  isMerchant: { type: 'boolean', probability: 0.01 },
} });

test('sends the authenticated inquiry and accepts correct decisions', async () => {
  const result = await sendInquiry('test-only-key', async (url, options) => {
    assert.equal(url, endpoint);
    assert.equal(options.method, 'POST');
    assert.equal(options.headers.Authorization, 'Bearer test-only-key');
    assert.equal(options.redirect, 'error');
    assert.deepEqual(JSON.parse(options.body), inquiry);
    return Response.json(good());
  });
  validateResponse(result);
});

test('rejects malformed and semantically incorrect answers', () => {
  for (const probability of [undefined, '0.99', NaN, -0.1, 1.1, 0.5]) {
    const result = good();
    result.answers.isGuard.probability = probability;
    assert.throws(() => validateResponse(result));
  }
  const wrong = good();
  wrong.answers.isMerchant.probability = 0.9;
  assert.throws(() => validateResponse(wrong));
  assert.throws(() => validateResponse({}));
  assert.throws(() => validateResponse({ ...good(), model: 'different-model' }));
});

test('missing credentials make no network request', async () => {
  await assert.rejects(sendInquiry('', () => assert.fail('Must not send')), /Missing AI_GATEWAY_API_KEY/);
});

test('HTTP failures omit raw server error bodies', async () => {
  await assert.rejects(sendInquiry('test-only-key', async () =>
    new Response('secret server body', { status: 401 })), /^Error: HTTP 401\. Check/);
});

test('invalid JSON and network errors fail the request', async () => {
  await assert.rejects(sendInquiry('test-only-key', async () => new Response('<html>oops</html>')), /invalid JSON/);
  await assert.rejects(sendInquiry('test-only-key', async () => { throw new Error('network offline'); }), /network offline/);
});

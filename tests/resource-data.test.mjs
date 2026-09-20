import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

// Parse actual C++ output with the same JSON parser used by the Node Jev bridge.
const output = execFileSync(process.argv[2], { encoding: 'utf8' });
const [empty, initial, changed, special] = output.trim().split(/\r?\n/).map(JSON.parse);
const rules = 'Named pools track available amounts and capacities. Values stay between zero and maximum. '
  + 'Spending requires the full amount; adjustments clamp to bounds. No automatic regeneration or depletion effects.';
const healthRules = 'Remaining vitality. Zero means depleted; death is not automatic.';
const manaRules = 'Pool reserved for mana. No spell costs are configured.';
const customRules = 'Custom resource pool; gameplay purpose is unspecified.';
assert.deepEqual(empty, { component: 'Resource', rulesDescription: rules, pools: {} });
assert.deepEqual(initial, {
  component: 'Resource',
  rulesDescription: rules,
  pools: {
    health: { current: 87.5, maximum: 100, rulesDescription: healthRules },
    mana: { current: 0, maximum: 0, rulesDescription: manaRules },
  },
});
assert.equal(changed.pools.health.current, 80);
assert.equal(changed.pools.health.rulesDescription, healthRules);
assert.deepEqual(special.pools['quote"slash\\\n\t\r\b\f\x01'], {
  current: 0.125, maximum: 12.5, rulesDescription: customRules,
});
assert.deepEqual(special.pools['caf\u00e9\u6c34\ud83d\udc3a'], {
  current: 1, maximum: 1, rulesDescription: customRules,
});
assert.equal(Math.fround(special.pools.extreme.current), 2 ** -149);
assert.equal(Math.fround(special.pools.extreme.maximum), Math.fround(3.4028234663852886e38));
console.log('Resource JSON data tests passed.');

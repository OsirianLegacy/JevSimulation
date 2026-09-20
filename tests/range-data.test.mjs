import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const output = execFileSync(process.argv[2], { encoding: 'utf8' });
const [range, moved, zero] = output.trim().split(/\r?\n/).map(JSON.parse);
assert.equal(range.component, 'Range');
assert.equal(range.radius, 2);
assert.equal(range.cardinalCost, 1);
assert.equal(range.diagonalCost, 2);
assert.deepEqual(range.center, { x: 4, y: -2 });
assert.equal(typeof range.rulesDescription, 'string');
assert.ok(range.rulesDescription.includes('abs(dx) + abs(dy) <= radius'));
assert.ok(range.rulesDescription.includes('Includes the center and boundary'));
assert.deepEqual(moved, { ...range, center: { x: 5, y: -1 } });
assert.equal(zero.radius, 0);
assert.deepEqual(zero.center, { x: 0, y: 0 });
console.log('Range component and JSON tests passed.');

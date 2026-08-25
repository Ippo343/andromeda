// Smoke test for the test infra itself (node --test, zero npm dependencies).
// Real UI logic gets its own test file (controls-logic.test.js) once
// data/js/controls-logic.js exists.
const { test } = require('node:test');
const assert = require('node:assert/strict');

test('node:test runner is wired up', () => {
    assert.equal(1 + 1, 2);
});

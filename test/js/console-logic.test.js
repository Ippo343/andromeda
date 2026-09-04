const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const { buildNetInfoCommand, appendLogLine } = require('../../web-installer/js/console-logic.js');

describe('buildNetInfoCommand', () => {
    test('emits the exact JSON line main.cpp\'s processSerialCommands expects', () => {
        assert.equal(buildNetInfoCommand(), '{"type":"netinfo"}\n');
    });
});

describe('appendLogLine', () => {
    test('appends under the cap without dropping anything', () => {
        assert.deepEqual(appendLogLine(['a', 'b'], 'c', 5), ['a', 'b', 'c']);
    });

    test('drops the oldest lines once over the cap', () => {
        assert.deepEqual(appendLogLine(['a', 'b', 'c'], 'd', 3), ['b', 'c', 'd']);
    });

    test('does not mutate the input array', () => {
        const lines = ['a', 'b'];
        appendLogLine(lines, 'c', 5);
        assert.deepEqual(lines, ['a', 'b']);
    });

    test('starting from empty stays under the cap', () => {
        assert.deepEqual(appendLogLine([], 'a', 3), ['a']);
    });
});

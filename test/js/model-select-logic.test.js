const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    buildModelOptions,
    buildModelCommand,
    buildRebootCommand,
} = require('../../web-installer/js/model-select-logic.js');

describe('buildModelOptions', () => {
    test('maps each model to a {value, label} pair', () => {
        const options = buildModelOptions([
            { id: 0x0300, label: 'L70 MK1' },
            { id: 0x0301, label: 'L10 MK0' },
        ]);
        assert.deepEqual(options, [
            { value: '768', label: 'L70 MK1' },
            { value: '769', label: 'L10 MK0' },
        ]);
    });

    test('returns an empty list for missing/malformed input', () => {
        assert.deepEqual(buildModelOptions(null), []);
        assert.deepEqual(buildModelOptions(undefined), []);
        assert.deepEqual(buildModelOptions('nope'), []);
    });
});

describe('buildModelCommand', () => {
    test('emits the exact JSON line WsCommandParser::parse expects for MODEL', () => {
        assert.equal(buildModelCommand(770), '{"type":"model","id":770}\n');
    });
});

describe('buildRebootCommand', () => {
    test('emits the exact JSON line WsCommandParser::parse expects for REBOOT', () => {
        assert.equal(buildRebootCommand(), '{"type":"reboot"}\n');
    });
});

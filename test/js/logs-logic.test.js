const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const {
    parseLogLine,
    levelClass,
    LOG_POLL_MS,
    MAX_LOG_LINES,
    logSignature,
    tailLines,
    logStatusLabel,
} = require('../../data/js/logs-logic.js');

describe('parseLogLine', () => {
    test('splits a well-formed line into ts / level / message', () => {
        const r = parseLogLine('123456 | INF | WiFi connected, IP 192.168.1.42');
        assert.deepEqual(r, {
            ts: 123456,
            level: 'INF',
            levelClass: 'log-inf',
            message: 'WiFi connected, IP 192.168.1.42',
        });
    });

    test('maps every known level tag to its class', () => {
        const cls = (tag) => parseLogLine(`1 | ${tag} | x`).levelClass;
        assert.equal(cls('FTL'), 'log-ftl');
        assert.equal(cls('ERR'), 'log-err');
        assert.equal(cls('WRN'), 'log-wrn');
        assert.equal(cls('INF'), 'log-inf');
        assert.equal(cls('TRC'), 'log-trc');
        assert.equal(cls('VRB'), 'log-vrb');
        assert.equal(cls('UNK'), 'log-raw');
    });

    test('keeps a pipe in the message intact', () => {
        assert.equal(parseLogLine('7 | WRN | a | b | c').message, 'a | b | c');
    });

    test('falls back to { raw } for a blank or non-matching line', () => {
        assert.deepEqual(parseLogLine(''), { raw: '', levelClass: 'log-raw' });
        assert.deepEqual(
            parseLogLine('    ...continuation without a prefix'),
            { raw: '    ...continuation without a prefix', levelClass: 'log-raw' });
    });
});

describe('levelClass', () => {
    test('is case-insensitive', () => {
        assert.equal(levelClass('err'), 'log-err');
    });
    test('unknown / empty -> log-raw', () => {
        assert.equal(levelClass('zzz'), 'log-raw');
        assert.equal(levelClass(''), 'log-raw');
        assert.equal(levelClass(undefined), 'log-raw');
    });
});

describe('logSignature', () => {
    test('identical text -> identical signature', () => {
        const text = 'a\nb\nc';
        assert.equal(logSignature(text), logSignature(text.slice()));
    });
    test('an appended line changes the signature', () => {
        assert.notEqual(logSignature('a\nb\nc'), logSignature('a\nb\nc\nd'));
    });
    test('empty / undefined text is stable and does not throw', () => {
        assert.equal(logSignature(''), logSignature(undefined));
    });
    test('a change beyond the tail window still changes the signature (length differs)', () => {
        const base = 'x'.repeat(200);
        assert.notEqual(logSignature('A' + base), logSignature('B' + base.slice(1)));
    });
});

describe('tailLines', () => {
    test('returns all lines when under the cap', () => {
        assert.deepEqual(tailLines('a\nb\nc', 10), ['a', 'b', 'c']);
    });
    test('caps to the last N lines when over the cap', () => {
        const text = Array.from({ length: 10 }, (_, i) => `line${i}`).join('\n');
        assert.deepEqual(tailLines(text, 3), ['line7', 'line8', 'line9']);
    });
    test('drops blank lines (including a trailing newline)', () => {
        assert.deepEqual(tailLines('a\nb\n\n', 10), ['a', 'b']);
    });
    test('empty / undefined text -> []', () => {
        assert.deepEqual(tailLines('', 10), []);
        assert.deepEqual(tailLines(undefined, 10), []);
    });
});

describe('logStatusLabel', () => {
    test('shown == total: "showing all N lines"', () => {
        assert.equal(logStatusLabel(5, 5, true), 'showing all 5 lines · auto-refreshing every 5s');
    });
    test('singular line count', () => {
        assert.equal(logStatusLabel(1, 1, true), 'showing all 1 line · auto-refreshing every 5s');
    });
    test('shown < total: "showing the last N of M lines"', () => {
        assert.equal(
            logStatusLabel(500, 812, true),
            'showing the last 500 of 812 lines · auto-refreshing every 5s');
    });
    test('paused wording when auto-refresh is off', () => {
        assert.equal(logStatusLabel(5, 5, false), 'showing all 5 lines · auto-refresh paused');
    });
});

describe('constants', () => {
    test('LOG_POLL_MS and MAX_LOG_LINES are the documented defaults', () => {
        assert.equal(LOG_POLL_MS, 5000);
        assert.equal(MAX_LOG_LINES, 500);
    });
});

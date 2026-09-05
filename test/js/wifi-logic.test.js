const { test, describe } = require('node:test');
const assert = require('node:assert/strict');
const { formatDeviceAddresses, getConnectedSsid } = require('../../data/js/wifi-logic.js');

describe('formatDeviceAddresses', () => {
    test('prefixes every host with http:// and keeps order (primary first)', () => {
        const info = {
            name: 'Kitchen',
            hosts: ['kitchen.local', 'l70-a1b2.local', 'andromeda-a1b2.local', 'andromeda.local'],
        };
        assert.deepEqual(formatDeviceAddresses(info), [
            'http://kitchen.local',
            'http://l70-a1b2.local',
            'http://andromeda-a1b2.local',
            'http://andromeda.local',
        ]);
    });

    test('returns an empty array when hosts is missing', () => {
        assert.deepEqual(formatDeviceAddresses({ name: 'Kitchen' }), []);
    });

    test('returns an empty array for a null/undefined payload', () => {
        assert.deepEqual(formatDeviceAddresses(null), []);
        assert.deepEqual(formatDeviceAddresses(undefined), []);
    });

    test('returns an empty array when hosts is not an array', () => {
        assert.deepEqual(formatDeviceAddresses({ hosts: 'kitchen.local' }), []);
    });

    test('drops non-string / empty entries rather than producing "http://"', () => {
        assert.deepEqual(formatDeviceAddresses({ hosts: ['kitchen.local', '', null, 42] }), [
            'http://kitchen.local',
        ]);
    });
});

describe('getConnectedSsid', () => {
    test('returns the SSID when connectedSsid is a non-empty string', () => {
        assert.equal(getConnectedSsid({ connectedSsid: 'HomeNet' }), 'HomeNet');
    });

    test('returns null when connectedSsid is empty (AP mode)', () => {
        assert.equal(getConnectedSsid({ connectedSsid: '' }), null);
    });

    test('returns null when connectedSsid is only whitespace', () => {
        assert.equal(getConnectedSsid({ connectedSsid: '   ' }), null);
    });

    test('returns null when connectedSsid is missing (older firmware)', () => {
        assert.equal(getConnectedSsid({ name: 'Kitchen' }), null);
    });

    test('returns null for a null/undefined payload', () => {
        assert.equal(getConnectedSsid(null), null);
        assert.equal(getConnectedSsid(undefined), null);
    });

    test('returns null when connectedSsid is not a string', () => {
        assert.equal(getConnectedSsid({ connectedSsid: 42 }), null);
    });
});

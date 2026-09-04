// The models the "set model" step can offer (#105/#187) - a hand-maintained
// mirror of include/geometry/model_config.h's registered models. Excludes
// only GRID_TEST_DEVICE (simulator-only - compiled out of hardware builds
// entirely, see model_config.h's comment; getModelConfig() would reject it
// on a real device) and ModelId::UNKNOWN. SINGLE_STRIP_TEST_DEVICE is a real,
// registered-on-hardware bench rig, so it's offered too.
//
// Kept in sync with model_config.h by test/test_native_suite/
// test_installer_model_list.cpp, which cross-checks every id here against
// the real registry (getModelConfig() must accept it) and flags any
// registered, shippable (Andromeda/L-Series) model missing from this list.
const MODELS = [
    { id: 0x0100, label: 'Single Strip Test Rig' },
    { id: 0x0200, label: 'Andromeda MK0' },
    { id: 0x0300, label: 'L70 MK1' },
    { id: 0x0301, label: 'L10 MK0' },
    { id: 0x0302, label: 'L10 MK1' },
];

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { MODELS };
}

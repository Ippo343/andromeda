#include "geometry/model_registry.h"
#include <Arduino.h>

// External model configurations
// Each model's namespace exposes a CONFIG constant
namespace AndromedaMk1 { extern const ModelConfig CONFIG; }
namespace AndromedaMk2 { extern const ModelConfig CONFIG; }
// TODO: Add extern declarations for additional models
// namespace L10 { extern const ModelConfig CONFIG; }
// namespace L25 { extern const ModelConfig CONFIG; }
// namespace L70 { extern const ModelConfig CONFIG; }
// namespace H10 { extern const ModelConfig CONFIG; }

// Global registry of all available models
const ModelConfig* MODEL_REGISTRY[] = {
    &AndromedaMk1::CONFIG,
    &AndromedaMk2::CONFIG,
    // TODO: Add additional models here
    // &L10::CONFIG,
    // &L25::CONFIG,
    // &L70::CONFIG,
    // &H10::CONFIG,
};

const uint8_t NUM_MODELS = sizeof(MODEL_REGISTRY) / sizeof(MODEL_REGISTRY[0]);

// Helper function to find a model config by ID
const ModelConfig* getModelConfig(ModelId id) {
    for (uint8_t i = 0; i < NUM_MODELS; i++) {
        if (MODEL_REGISTRY[i]->id == id) {
            return MODEL_REGISTRY[i];
        }
    }
    return nullptr;
}

// Helper function to get full model name as string
const char* getModelName(ModelId id) {
    const ModelConfig* config = getModelConfig(id);
    if (config) {
        static char name_buffer[32];
        snprintf(name_buffer, sizeof(name_buffer), "%s %s",
                 config->family, config->model_name);
        return name_buffer;
    }
    return "Unknown";
}

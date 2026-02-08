#include "geometry/model_registry.h"
#include <Arduino.h>

// External model configurations
// Each model's namespace exposes a CONFIG constant
namespace AndromedaMk1 { extern const ModelConfig CONFIG; }
namespace SingleStripTestDevice { extern const ModelConfig CONFIG; }

// Global registry of all available models
const ModelConfig* MODEL_REGISTRY[] = {
    &AndromedaMk1::CONFIG,
    &SingleStripTestDevice::CONFIG,
};

const size_t NUM_MODELS = sizeof(MODEL_REGISTRY) / sizeof(MODEL_REGISTRY[0]);

// Helper function to find a model config by ID
const ModelConfig* getModelConfig(ModelId id) {
    for (size_t i = 0; i < NUM_MODELS; i++) {
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
        snprintf(name_buffer, sizeof(name_buffer), "%s", config->name);
        return name_buffer;
    }
    return "Unknown";
}

#pragma once

#include "model_config.h"

// TODO: this can probably be private in Geometry

// Global registry of all available models
extern const ModelConfig* MODEL_REGISTRY[];
extern const uint8_t NUM_MODELS;

// Helper function to find a model config by ID
const ModelConfig* getModelConfig(ModelId id);

// Helper function to get model name as string
const char* getModelName(ModelId id);

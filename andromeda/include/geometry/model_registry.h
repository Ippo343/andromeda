#pragma once

#include "model_config.h"

// Global registry of all available models
// Populated by each model's implementation file
extern const ModelConfig* MODEL_REGISTRY[];
extern const uint8_t NUM_MODELS;

// Helper function to find a model config by ID
const ModelConfig* getModelConfig(ModelId id);

// Helper function to get model name as string
const char* getModelName(ModelId id);

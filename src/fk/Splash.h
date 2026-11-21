// FluxKit - boot splash (internal)
#pragma once
#include "App.h"
#include "Layer.h"
namespace fk { namespace splash {
Layer* create(Layer* parent, int W, int H, const Config& cfg);  // gradient + boot text + credit, drawn at once
void release(Layer* splash);                                    // free its surfaces after the fade
}}

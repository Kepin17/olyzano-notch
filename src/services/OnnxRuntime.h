#pragma once

#include "services/Matting.h"

#include <QString>

// Running an ONNX model with ONNX Runtime, which is a downloaded helper (Settings → AI), loaded when it is needed. This build's wrapper is made
// when ONNX Runtime's C header is in third_party/onnxruntime; without it `supported()` is false and nothing can be run.
namespace OnnxRuntime {

bool supported();

// A runner for the model file, using the library file (libonnxruntime.so / onnxruntime.dll). Empty (and *error set) when it cannot be made.
Matting::Runner makeRunner(const QString &library, const QString &model, QString *error);

}

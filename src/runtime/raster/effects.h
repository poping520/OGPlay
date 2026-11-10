#pragma once

#include "include/core/SkMatrix.h"
#include "include/core/SkShader.h"

#include <functional>

namespace ogplay::runtime::raster {

struct Shader {
    using Source = std::function<sk_sp<SkShader>(const SkSamplingOptions&)>;

    explicit Shader(Source source) : source(std::move(source)) {}

    [[nodiscard]] sk_sp<SkShader> Resolve(const SkSamplingOptions& sampling) const {
        auto shader = source(sampling);
        return matrix.isIdentity() ? shader : shader->makeWithLocalMatrix(matrix);
    }

    Source source;
    SkMatrix matrix = SkMatrix::I();
};

float BlurSigma(float radius);

}  // namespace ogplay::runtime::raster

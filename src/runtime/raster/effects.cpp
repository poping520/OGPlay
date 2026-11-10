#include "effects.h"

namespace ogplay::runtime::raster {

float BlurSigma(float radius) {
    return radius > 0 ? radius * 0.57735F + 0.5F : 0.F;
}

}  // namespace ogplay::runtime::raster

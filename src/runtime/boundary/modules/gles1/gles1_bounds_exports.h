#pragma once

// name, stable module-local id, A32 word parameter count, concrete method
#define OGPLAY_GLES1_BOUNDS_EXPORTS(X)                                         \
    X("glColorPointerBounds", 148, 5, ColorPointerBounds)                     \
    X("glNormalPointerBounds", 149, 4, NormalPointerBounds)                   \
    X("glTexCoordPointerBounds", 150, 5, TexCoordPointerBounds)               \
    X("glVertexPointerBounds", 151, 5, VertexPointerBounds)                   \
    X("glPointSizePointerOESBounds", 152, 4, PointSizePointerOesBounds)       \
    X("glMatrixIndexPointerOESBounds", 153, 5, MatrixIndexPointerOesBounds)   \
    X("glWeightPointerOESBounds", 154, 5, WeightPointerOesBounds)

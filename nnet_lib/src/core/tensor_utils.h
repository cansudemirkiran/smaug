#ifndef _CORE_TENSOR_UTILS_H_
#define _CORE_TENSOR_UTILS_H_

#include <iostream>
#include <vector>
#include <cstring>

#include "core/tensor.h"

namespace smaug {

class Workspace;
class Operator;

std::ostream& operator<<(std::ostream& os, const TensorIndexIterator& iter);
std::ostream& operator<<(std::ostream& os, const TensorShape& shape);
std::ostream& operator<<(std::ostream& os, const Tensor& tensor);

template <typename DType>
void printTensorElement(std::ostream& os, DType* data, int index) {
    os << data[index];
}

template <>
void printTensorElement<float16>(std::ostream& os, float16* data, int index);

template <typename DType>
void writeTensorToOstream(std::ostream& os, const Tensor& tensor) {
    const TensorShape& shape = tensor.getShape();
    if (shape.ndims() == 0) {
        os << "  [ ]\n";
        return;
    }
    int ndims = shape.ndims();
    int newlineAfterElems = shape[ndims - 1];
    int newGroupAfterElems =
            (shape.ndims() >= 2 ? shape[ndims - 1] * shape[ndims - 2]
                                : shape[ndims - 1]);
    int counter = 0;
    DType* data = tensor.template data<DType>();
    os << tensor.getName() << ", shape = " << shape << "\n";
    for (auto idx = tensor.startIndex(); !idx.end(); ++idx) {
        // Print the current index after going through all of the last two
        // dimensions.
        if (counter == 0)
            os << idx << "\n[ ";
        printTensorElement<DType>(os, data, idx);
        os << " ";
        ++counter;
        if (counter % newGroupAfterElems == 0) {
            counter = 0;
            os << " ]\n";
        } else if (counter % newlineAfterElems == 0) {
            os << "\n  ";
        }
    }
}

namespace internal {

template <typename DType>
void copyTensorRegion(Tensor* dest,
                      Tensor* src,
                      const std::vector<int>& destOrigin,
                      const std::vector<int>& srcOrigin,
                      const std::vector<int>& regionSize) {
    const TensorShape& srcShape = src->getShape();
    const TensorShape& destShape = dest->getShape();
    TensorShape regionShape(
            regionSize, srcShape.getLayout(), srcShape.getAlignment());
    const int ndims = srcShape.ndims();
    auto destIt = TensorRegionIndexIterator(destShape, destOrigin, regionSize);
    auto srcIt = TensorRegionIndexIterator(srcShape, srcOrigin, regionSize);

    // We know where to copy data from and how much data we should copy (the
    // data region), now starting from the last dimension, we figure out how
    // much contiguous data there exists such that we can apply more efficient
    // data copy mechanisms (memcpy).
    std::vector<int> contiguousRegion(ndims, 1);
    int contiguousSize = 1;
    for (int i = ndims - 1; i >= 0; i--) {
        contiguousSize *= regionShape.getStorageDim(i);
        contiguousRegion[i] = regionShape[i];
        // If we find a region dimension smaller than that of either src or
        // dest tensor, then the next region dimension must not be contiguous.
        if (regionShape[i] < srcShape[i] || regionShape[i] < destShape[i])
            break;
    }

    // Copy the data region from the src tensor to the dest tensor.
    DType* destPtr = dest->template data<DType>();
    DType* srcPtr = src->template data<DType>();
    while (!srcIt.end() && !destIt.end()) {
#ifdef PEDANTIC
        destPtr[destIt] = srcPtr[srcIt];
        ++destIt;
        ++srcIt;
#else
        memcpy(&destPtr[destIt],
               &srcPtr[srcIt],
               contiguousSize * sizeof(DType));
        destIt += contiguousRegion;
        srcIt += contiguousRegion;
#endif
    }
}

// The difference between this and the above one is that this copies data
// linearly from the tensor to another tensor, whereas the copy in the above one
// is dimension specific.
template <typename DType>
void copyRawTensorData(Tensor* dest,
                       Tensor* src,
                       int destOffset,
                       int srcOffset,
                       int copySize) {
    DType* destPtr = dest->template data<DType>();
    DType* srcPtr = src->template data<DType>();
    std::memcpy(
            &destPtr[destOffset], &srcPtr[srcOffset], copySize * sizeof(DType));
}

}  // namespace internal

void copyTensorRegion(Tensor* dest,
                      Tensor* src,
                      std::vector<int> destOrigin,
                      std::vector<int> srcOrigin,
                      std::vector<int> regionSize);

void copyRawTensorData(
        Tensor* dest, Tensor* src, int destOffset, int srcOffset, int copySize);

// This generates a TiledTensor from a Tensor using the specified tile shape.
TiledTensor generateTiledTensor(Tensor* tensor,
                                const TensorShape& tileShape,
                                Operator* op,
                                int filedRows = 0,
                                int filedCols = 0,
                                int rowStride = 1,
                                int colStride = 1,
                                PaddingType paddingType = ValidPadding);

// This generates the tiles and copies data to them from the original tensor.
TiledTensor generateTiledTensorAndCopyData(
        Tensor* tensor,
        const TensorShape& tileShape,
        Operator* op,
        int fieldRows = 0,
        int fieldCols = 0,
        int rowStride = 1,
        int colStride = 1,
        PaddingType paddingType = ValidPadding);

// This generates the tiles and copies data to them from the original tensor.
template <typename... Args>
TiledTensor generateTiledTensorAndCopyData(Args&&... args) {
    TiledTensor tiledTensor =
            generateTiledTensor(std::forward<Args>(args)...);
    tiledTensor.copyDataToAllTiles();
    return tiledTensor;
}

// This will copy data from a tiled tensor into a single tensor. We name it as
// "untile" because what it does reverses the tiling process.
void untileTiledTensor(TiledTensor& tiledTensor, Tensor* destTensor);

// The difference between this and untileTiledTensor is:
//  - untileTiledTensor copies tensors into specific regions of memory
//    corresponding to their tile origins.
//  - flattenTiledTensor copies tensor data as a contiguous block into the
//    destination, as if only a single dimension existed.
void flattenTiledTensor(TiledTensor& tiledTensor, Tensor* destTensor);

// This concatenates tensors on the specified dimension into one single tensor.
Tensor* concatTensors(std::vector<Tensor*> inputTensors,
                      int concatDim,
                      Workspace* workspace);

}  // namespace smaug

#endif

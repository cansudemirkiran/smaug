#include "core/backend.h"
#include "operators/common.h"
#include "operators/smv/smv_inner_product_op.h"
#include "operators/smv/smv_inner_product_tiling.h"
#include "operators/smv/kernels.h"
#include "utility/debug_stream.h"

namespace smaug {
namespace smv {
namespace fc {

const int kNumPEs = 8;
const int kNumMaccsPerPE = 32;

}  // namespace fc
}  // namespace smv

// This function iterates the tiles generated by the tiling optimizer and send a
// tile triplet to the hardware kernel for computation. The tile iteration is in
// the following order:
// 1) N: batch-wise tiles in the inputs.
// 2) W: neuron-wise tiles in the weights.
// 3) A: activation-wise tiles in the inputs/weights.
void SmvInnerProductOp::runNWA(TiledTensor& inputs,
                               TiledTensor& weights,
                               TiledTensor& outputs) {
    // Ordinarily, we don't need to tile the outputs. If this fails, it means
    // the inner product has uncommonly large outputs, let's add the output
    // iteration when that happens.
    assert(outputs.size() == 1 &&
           "Inner product outputs tiling not implemented yet!");
    int inputActTiles = inputs.getShape()[1];
    int weightActTiles = weights.getShape()[1];
    int weightNeuronTiles = weights.getShape()[0];
    auto inputIdx = inputs.startIndex();
    auto weightIdx = weights.startIndex();
    auto outputIdx = outputs.startIndex();
    for (int N = 0; N < inputs.getShape()[0]; N++) {
        // Usually we are constrained by weights whereas outputs can fit in the
        // scratchpad. This keeps track of finished neurons and will be used by
        // the kernel for correct offset in the outputs scratchpad.
        int finishedNeurons = 0;
        for (int W = 0; W < weightNeuronTiles; W++) {
            Tensor* outputTile = outputs[outputIdx(N, 0)];
            const TensorShape& outputShape = outputTile->getShape();
            // When we finish the last neuron-wise tile, the partial sums become
            // complete and the outputs are sent back to the host memory.
            bool sendOutputs = W == weightNeuronTiles - 1;
            int iC = 0, wC = 0;
            // This keeps track of the activation offset of the inputs.
            int actOffset = 0;
            while (iC < inputActTiles && wC < weightActTiles) {
                // There is one condition on which the input tile has different
                // number of activations from the weight tile: the inputs don't
                // need tiling on activations while the weights do. In that
                // case, we send the input tile once and keep the input tile
                // stationary in the scrachpad, finishing the weight
                // activation-wise tiles with multiple invocations.
                dout(2) << "Input: " << inputIdx(N, iC)
                        << ", weights: " << weightIdx(W, wC)
                        << ", output: " << outputIdx(N, 0) << "\n";
                Tensor* inputTile = inputs[inputIdx(N, iC)];
                Tensor* weightsTile = weights[weightIdx(W, wC)];
                const TensorShape& inputShape = inputTile->getShape();
                const TensorShape& weightsShape = weightsTile->getShape();
                int inputDims[2] = { inputShape[0], inputShape[1] };
                int weightsDims[2] = { weightsShape[0], weightsShape[1] };
                int outputDims[2] = { outputShape[0], outputShape[1] };
                // If the input and weight tiles belong to the same channel
                // group, then their data will be loaded at the same time into
                // the spads, so we start from the beginning of the tile.
                // Otherwise, we start from the last place we left off from.
                int actStart = (iC == wC) ? 0 : actOffset;
                // If the weights are tiled on activations, this should be set
                // to true for non-first weight tiles to avoid resetting the
                // result buffer.
                bool accumulate = wC > 0;

                invokeKernel(smv::kInnerProductHw,
                             smv_matrix_multiply_transpose_nc_vec_fxp,
                             inputTile->data<float16>(),
                             weightsTile->data<float16>(),
                             outputTile->data<float16>(), smv::spad0,
                             smv::spad1, smv::spad2, inputDims, weightsDims,
                             outputDims, inputShape.getPadding(1),
                             weightsShape.getPadding(1),
                             outputShape.getPadding(1), actStart,
                             finishedNeurons, accumulate, sendOutputs);

                actOffset += weightsTile->getShape()[1];
                if (inputActTiles == weightActTiles) {
                    iC++;
                    wC++;
                } else if (inputActTiles == 1) {
                    wC++;
                } else {
                    assert(false && "The input/weight tiles can have different "
                                    "number of channels only when the inputs "
                                    "don't need activation-wise tiling.");
                }
            }
            finishedNeurons += weights[weightIdx(W, 0)]->getShape()[0];
        }
    }
}

void SmvInnerProductOp::run() {
    using namespace smaug::smv::fc;
    auto inputs = getInput(Inputs);
    auto weights = getInput(Weights);
    auto outputs = getOutput(Outputs);
    const TensorShape& inputsShape = inputs->getShape();
    const TensorShape& weightsShape = weights->getShape();
    const TensorShape& outputsShape = outputs->getShape();
    assert(inputsShape.getLayout() == DataLayout::NC);
    assert(weightsShape.getLayout() == DataLayout::NC);
    assert(outputsShape.getLayout() == DataLayout::NC);

    // We need to transpose the weights, because the SMV matrix multiplication
    // kernel (as well as the tiling optimizer) takes transposed weights.
    // TODO: Eventually, this should be done ahead of time, and we can detect
    // this just by checking whether the shapes are compatible.
    auto transposedWeights = transpose2DTensor<float16>(weights);
    workspace->addTensor(transposedWeights);
    setInput(transposedWeights, Weights);
    dout(2) << *transposedWeights << "\n";

    // This function will tile (if necessary) the input/weight/output tensors
    // of the inner product operator into smaller tensor tiles so that each tile
    // can fit in the corresponding scratchpad of the accelerator.
    std::array<TiledTensor, 3> tiledTensors = TilingOptimizer::doTiling(this);
    runNWA(tiledTensors[0], tiledTensors[1], tiledTensors[2]);
    untileTiledTensor(tiledTensors[2], outputs);
}

}  // namespace smaug
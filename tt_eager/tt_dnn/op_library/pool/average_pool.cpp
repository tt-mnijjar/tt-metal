#include "tt_dnn/op_library/pool/average_pool.hpp"
#include "tt_dnn/op_library/reduce/reduce_op.hpp"


namespace tt {
namespace tt_metal {

template<PoolType pool>
Tensor pool_2d(const Tensor& input) {
    TT_ASSERT(input.storage_type() == StorageType::DEVICE, "Input tensor needs to be on device");
    auto input_shape = input.shape();
    switch (pool) {
        case PoolType::AVG: {
            auto height_without_padding = input.shape().without_padding()[-2];
            return reduce(input, ReduceOpMath::SUM, ReduceOpDim::H, 1 / float(height_without_padding));
        }
        default:
            TT_ASSERT(false && "Undefined pool type");
    }
}

Tensor average_pool_2d(const Tensor& input) {
    TT_ASSERT(input.storage_type() == StorageType::DEVICE, "Input tensor needs to be on device");
    auto output = input;

    Shape in_shape = input.shape();
    auto input_padding = in_shape.padding();
    TT_ASSERT(input_padding[1].front == 0 and input_padding[1].back == 0);
    auto output_padding = Padding({input_padding[0], {0, 0}, {0, input_padding[2].back * in_shape[1]}, input_padding[3]}, input_padding.pad_value());
    auto output_shape = Shape({in_shape[0], 1, in_shape[1] * in_shape[2], in_shape[3]}, output_padding);
    output = output.reshape(output_shape);

    output = pool_2d<PoolType::AVG>(output);
    return output;
}

}  // namespace tt_metal
}  // namespace tt
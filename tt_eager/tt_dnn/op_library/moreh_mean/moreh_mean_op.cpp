// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_dnn/op_library/moreh_mean/moreh_mean_op.hpp"

#include "tt_dnn/op_library/reduce/reduce_op.hpp"
#include "tt_eager/tt_dnn/op_library/moreh_helper_functions.hpp"
#include "tt_metal/common/constants.hpp"
#include "tt_metal/host_api.hpp"

namespace tt {
using namespace constants;
namespace operations {
namespace primary {

////////////////////////////////////////////////////////////////////////////
//                         MorehMean
////////////////////////////////////////////////////////////////////////////
void MorehMean::validate(const std::vector<Tensor>& inputs) const {
    TT_ASSERT((dim >= 0 && dim <= 3), "dim should be 0 - 3");
    const auto& input = inputs.at(0);
    const auto& output = inputs.at(1);

    auto input_shape = input.shape();
    const auto& output_shape = output.shape();
    auto input_shape_wo_padding = input.shape().without_padding();
    const auto& output_shape_wo_padding = output.shape().without_padding();

    if (dim == 0 || dim == 1) {
        input_shape[dim] = 1;
        input_shape_wo_padding[dim] = 1;
    } else {
        input_shape[dim] = TILE_HEIGHT;
        input_shape_wo_padding[dim] = 1;
    }

    for (int i = 0; i < input_shape.rank(); ++i) {
        TT_ASSERT(input_shape[i] == output_shape[i]);
        TT_ASSERT(input_shape_wo_padding[i] == output_shape_wo_padding[i]);
    }
}

std::vector<Tensor> MorehMean::create_output_tensors(const std::vector<Tensor>& inputs) const {
    // Inplace
    return {};
}

std::vector<Shape> MorehMean::compute_output_shapes(const std::vector<Tensor>& inputs) const {
    // Inplace
    return {};
}

operation::ProgramWithCallbacks MorehMean::create_program(
    const std::vector<Tensor>& inputs, std::vector<Tensor>& outputs) const {
    TT_ASSERT((dim >= 0 && dim <= 3), "dim should be 0 - 3");
    auto& input = inputs.at(0);
    auto& output = inputs.at(1);

    if (dim == 0 || dim == 1) {
        return moreh_mean_nc(input, output, dim);
    } else if (dim == 2) {
        return moreh_mean_h(input, output);
    } else {
        return moreh_mean_w(input, output);
    }
}

inline Shape compute_output_shape(const Shape& input_shape, const int64_t& dim) {
    auto output_shape = input_shape;
    auto padding = output_shape.padding();
    switch (dim) {
        case 0:
        case 1: output_shape[dim] = 1; break;
        case 2:
            output_shape[dim] = TILE_HEIGHT;
            padding[dim] = Padding::PadDimension{0, 31};
            break;
        case 3:
            output_shape[dim] = TILE_WIDTH;
            padding[dim] = Padding::PadDimension{0, 31};
            break;
    }

    return {Shape(output_shape, padding)};
}

inline Tensor create_output_tensor(
    const Tensor& input_tensor, const Shape& output_shape, const MemoryConfig& mem_config) {
    TT_ASSERT(input_tensor.storage_type() == StorageType::DEVICE);
    return create_device_tensor(output_shape, input_tensor.dtype(), Layout::TILE, input_tensor.device(), mem_config);
}

// output as arg
Tensor moreh_mean_(const Tensor& input, const Tensor& output, const int64_t& dim) {
    operation::run(MorehMean{.dim = dim}, {input, output});
    return output;
}

// output creation inside
Tensor moreh_mean_(const Tensor& input, const int64_t& dim, const MemoryConfig& mem_config) {
    const auto& input_shape = input.shape();
    const auto& output_shape = compute_output_shape(input_shape, dim);
    auto output = create_output_tensor(input, output_shape, mem_config);

    const auto& output_shape_wo_padding = output.shape().without_padding();
    operation::run(MorehMean{.dim = dim}, {input, output});
    return output;
}

Tensor moreh_mean(
    const Tensor& input,
    const Tensor& output,
    std::vector<int64_t>& dims,
    const MemoryConfig& mem_config) {
    // reduce for all dims
    if (dims.empty()) {
        dims = {0, 1, 2, 3};
    }

    std::vector<int64_t> sorted_dims = dims;
    std::sort(sorted_dims.begin(), sorted_dims.end());

    auto temp_input = input;
    for (uint32_t i = dims.size() - 1; i > 0; i--) {
        log_debug(LogTest, "{}:{} dim {}", __func__, __LINE__, sorted_dims[i]);
        auto temp_output = moreh_mean_(temp_input, sorted_dims[i], mem_config);
        temp_input = temp_output;
    }
    log_debug(LogTest, "{}:{} dim {}", __func__, __LINE__, sorted_dims.front());
    moreh_mean_(temp_input, output, sorted_dims.front());
    return output;
}

}  // namespace primary
}  // namespace operations
}  // namespace tt

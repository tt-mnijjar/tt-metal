// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_eager/tt_dnn/op_library/moreh_groupnorm/moreh_groupnorm_op.hpp"

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace tt {

namespace operations {

namespace primary {

namespace {

inline void check_tensor(const Tensor &tensor, const std::string &op_name) {
    TT_ASSERT(tensor.layout() == Layout::TILE, fmt::format("{} only supports tiled layout.", op_name));
    TT_ASSERT(tensor.dtype() == DataType::BFLOAT16, fmt::format("{} only supports bfloat16.", op_name));
    TT_ASSERT(
        tensor.storage_type() == StorageType::DEVICE, fmt::format("Operands to {} need to be on device!", op_name));
    TT_ASSERT(
        tensor.buffer() != nullptr, fmt::format("Operands to {} need to be allocated in buffers on device!", op_name));
}

inline void check_tensor(std::optional<Tensor> tensor, const std::string &op_name) {
    if (!tensor.has_value()) {
        return;
    }
    check_tensor(tensor.value(), op_name);
}

}  // namespace

void MorehGroupNorm::validate_with_output_tensors(
    const std::vector<Tensor> &input_tensors,
    const std::vector<std::optional<const Tensor>> &optional_input_tensors,
    const std::vector<std::optional<Tensor>> &output_tensors) const {
    const auto &input = input_tensors.at(0);

    auto output = output_tensors.at(0);
    auto mean = output_tensors.at(1);
    auto rstd = output_tensors.at(2);

    auto gamma = optional_input_tensors.at(0);
    auto beta = optional_input_tensors.at(1);

    check_tensor(input, "moreh_groupnorm");

    check_tensor(output, "moreh_groupnorm");
    check_tensor(mean, "moreh_groupnorm");
    check_tensor(rstd, "moreh_groupnorm");

    check_tensor(gamma, "moreh_groupnorm");
    check_tensor(beta, "moreh_groupnorm");

    // input (N, C, H, W)
    auto C = input.shape()[1];
    TT_ASSERT(C % this->num_groups == 0, "input_shape[1] must be divisible by num_groups.");
    // output (N, C, H, W)
    if (output.has_value()) {
        C = output.value().shape()[1];
        TT_ASSERT(C % this->num_groups == 0, "output_shape[1] must be divisible by num_groups.");
    }
    // gamma (1, 1, 1, C)
    if (gamma.has_value()) {
        C = gamma.value().shape().without_padding()[-1];
        TT_ASSERT(C % this->num_groups == 0, "gamma_shape[-1] must be divisible by num_groups.");
    }
    // beta (1, 1, 1, C)
    if (beta.has_value()) {
        C = beta.value().shape().without_padding()[-1];
        TT_ASSERT(C % this->num_groups == 0, "beta_shape[-1] must be divisible by num_groups.");
    }

    // mean (1, 1, N, num_groups)
    if (mean.has_value()) {
        TT_ASSERT(
            mean.value().shape().without_padding()[-1] == this->num_groups, "mean_shape[-1] must match num_groups.");
    }
    // rstd (1, 1, N, num_groups)
    if (rstd.has_value()) {
        TT_ASSERT(
            rstd.value().shape().without_padding()[-1] == this->num_groups, "rstd_shape[-1] must match num_groups.");
    }
}

std::vector<Shape> MorehGroupNorm::compute_output_shapes(const std::vector<Tensor> &input_tensors) const {
    // mean, rstd (1, 1, N, num_groups)
    const auto output_shape = input_tensors.at(0).shape();
    const auto N = output_shape[0];
    const auto num_groups = this->num_groups;
    const std::vector<uint32_t> mean_rstd_origin_shape{
        1,
        1,
        TILE_HEIGHT * ((N + TILE_HEIGHT - 1) / TILE_HEIGHT),
        TILE_WIDTH * ((num_groups + TILE_WIDTH - 1) / TILE_WIDTH)};

    auto mean_rstd_padding = output_shape.padding();
    mean_rstd_padding[2] = Padding::PadDimension{0, TILE_HEIGHT - (N % TILE_HEIGHT)};
    mean_rstd_padding[3] = Padding::PadDimension{0, TILE_WIDTH - (num_groups % TILE_WIDTH)};

    Shape mean_rstd_shape(mean_rstd_origin_shape, mean_rstd_padding);
    return {output_shape, mean_rstd_shape, mean_rstd_shape};
}

std::vector<Tensor> MorehGroupNorm::create_output_tensors(
    const std::vector<Tensor> &input_tensors, const std::vector<std::optional<Tensor>> &output_tensors) const {
    const auto &output_shapes = this->compute_output_shapes(input_tensors);
    auto dtype = input_tensors.at(0).dtype();
    Layout layout{Layout::TILE};
    auto device = input_tensors.at(0).device();

    std::vector<Tensor> result;
    result.reserve(output_tensors.size());

    if (output_tensors.at(0).has_value()) {
        result.push_back(output_tensors.at(0).value());
    } else {
        result.push_back(create_device_tensor(output_shapes.at(0), dtype, layout, device, this->output_mem_config));
    }

    if (output_tensors.at(1).has_value()) {
        result.push_back(output_tensors.at(1).value());
    } else {
        result.push_back(create_device_tensor(output_shapes.at(1), dtype, layout, device, this->mean_mem_config));
    }

    if (output_tensors.at(2).has_value()) {
        result.push_back(output_tensors.at(2).value());
    } else {
        result.push_back(create_device_tensor(output_shapes.at(2), dtype, layout, device, this->rstd_mem_config));
    }

    return std::move(result);
}

operation::ProgramWithCallbacks MorehGroupNorm::create_program(
    const std::vector<Tensor> &input_tensors,
    const std::vector<std::optional<const Tensor>> &optional_input_tensors,
    std::vector<Tensor> &output_tensors) const {
    const auto &input = input_tensors.at(0);

    // TODO(seunghwan100): Make mean, rstd optional
    auto &output = output_tensors.at(0);
    auto &mean = output_tensors.at(1);
    auto &rstd = output_tensors.at(2);

    auto gamma = optional_input_tensors.at(0);
    auto beta = optional_input_tensors.at(1);

    return moreh_groupnorm_impl(input, this->num_groups, this->eps, output, mean, rstd, gamma, beta);
}

std::vector<Tensor> moreh_groupnorm(
    const Tensor &input,
    uint32_t num_groups,
    float eps,
    const std::optional<const Tensor> gamma,
    const std::optional<const Tensor> beta,
    const std::optional<const Tensor> output,
    const std::optional<const Tensor> mean,
    const std::optional<const Tensor> rstd,
    const MemoryConfig &output_mem_config,
    const MemoryConfig &mean_mem_config,
    const MemoryConfig &rstd_mem_config) {
    return operation::run(
        MorehGroupNorm{
            .num_groups = num_groups,
            .eps = eps,
            .output_mem_config = std::move(output_mem_config),
            .mean_mem_config = std::move(mean_mem_config),
            .rstd_mem_config = std::move(rstd_mem_config)},
        {input},
        {gamma, beta},
        {output, mean, rstd});
}

}  // namespace primary

}  // namespace operations

}  // namespace tt

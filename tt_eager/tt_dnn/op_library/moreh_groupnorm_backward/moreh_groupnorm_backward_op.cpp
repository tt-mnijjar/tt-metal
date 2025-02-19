// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_eager/tt_dnn/op_library/moreh_groupnorm_backward/moreh_groupnorm_backward_op.hpp"

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

void MorehGroupNormBackwardInputGrad::validate_with_output_tensors(
    const std::vector<Tensor> &input_tensors,
    const std::vector<std::optional<const Tensor>> &optional_input_tensors,
    const std::vector<std::optional<Tensor>> &output_tensors) const {
    const auto &output_grad = input_tensors.at(0);
    const auto &input = input_tensors.at(1);
    const auto &mean = input_tensors.at(2);
    const auto &rstd = input_tensors.at(3);

    auto &input_grad = output_tensors.at(0);

    auto gamma = optional_input_tensors.at(0);

    check_tensor(output_grad, "moreh_groupnorm_backward_input_grad");
    check_tensor(input, "moreh_groupnorm_backward_input_grad");
    check_tensor(mean, "moreh_groupnorm_backward_input_grad");
    check_tensor(rstd, "moreh_groupnorm_backward_input_grad");

    check_tensor(input_grad, "moreh_groupnorm_backward_input_grad");

    check_tensor(gamma, "moreh_groupnorm_backward_input_grad");

    // output_grad (N, C, H, W)
    auto C = output_grad.shape()[1];
    TT_ASSERT(C % this->num_groups == 0, "output_grad_shape[1] must be divisible by num_groups.");
    // input (N, C, H, W)
    C = input.shape()[1];
    TT_ASSERT(C % this->num_groups == 0, "input_shape[1] must be divisible by num_groups.");
    // input_grad (N, C, H, W)
    if (input_grad.has_value()) {
        C = input_grad.value().shape()[1];
        TT_ASSERT(C % this->num_groups == 0, "input_grad_shape[1] must be divisible by num_groups.");
    }
    // gamma (1, 1, 1, C)
    if (gamma.has_value()) {
        C = gamma.value().shape().without_padding()[-1];
        TT_ASSERT(C % this->num_groups == 0, "gamma_shape[-1] must be divisible by num_groups.");
    }

    // mean (1, 1, N, num_groups)
    TT_ASSERT(mean.shape().without_padding()[-1] == this->num_groups, "mean_shape[-1] must match num_groups.");
    // rstd (1, 1, N, num_groups)
    TT_ASSERT(rstd.shape().without_padding()[-1] == this->num_groups, "rstd_shape[-1] must match num_groups.");
}

std::vector<Shape> MorehGroupNormBackwardInputGrad::compute_output_shapes(
    const std::vector<Tensor> &input_tensors) const {
    return {input_tensors.at(0).shape()};
}

std::vector<Tensor> MorehGroupNormBackwardInputGrad::create_output_tensors(
    const std::vector<Tensor> &input_tensors, const std::vector<std::optional<Tensor>> &output_tensors) const {
    if (output_tensors.at(0).has_value()) {
        return {output_tensors.at(0).value()};
    }

    return operation::generic_create_output_tensors(
        *this, input_tensors, input_tensors.at(0).dtype(), Layout::TILE, this->input_grad_mem_config);
}

operation::ProgramWithCallbacks MorehGroupNormBackwardInputGrad::create_program(
    const std::vector<Tensor> &input_tensors,
    const std::vector<std::optional<const Tensor>> &optional_input_tensors,
    std::vector<Tensor> &output_tensors) const {
    const auto &output_grad = input_tensors.at(0);
    const auto &input = input_tensors.at(1);
    const auto &mean = input_tensors.at(2);
    const auto &rstd = input_tensors.at(3);

    auto &input_grad = output_tensors.at(0);

    auto gamma = optional_input_tensors.at(0);

    return moreh_groupnorm_backward_input_grad_impl(
        output_grad, input, mean, rstd, this->num_groups, input_grad, gamma);
}

Tensor moreh_groupnorm_backward_input_grad(
    const Tensor &output_grad,
    const Tensor &input,
    const Tensor &mean,
    const Tensor &rstd,
    uint32_t num_groups,
    const std::optional<const Tensor> gamma,
    const std::optional<const Tensor> input_grad,
    const MemoryConfig &input_grad_mem_config) {
    return operation::run(
               MorehGroupNormBackwardInputGrad{
                   .num_groups = num_groups, .input_grad_mem_config = std::move(input_grad_mem_config)},
               {output_grad, input, mean, rstd},
               {gamma},
               {input_grad})
        .at(0);
}

void MorehGroupNormBackwardGammaBetaGrad::validate_with_output_tensors(
    const std::vector<Tensor> &input_tensors, const std::vector<std::optional<Tensor>> &output_tensors) const {
    const auto &output_grad = input_tensors.at(0);
    const auto &input = input_tensors.at(1);
    const auto &mean = input_tensors.at(2);
    const auto &rstd = input_tensors.at(3);

    auto &gamma_grad = output_tensors.at(0);
    auto &beta_grad = output_tensors.at(1);

    check_tensor(output_grad, "moreh_groupnorm_backward_gamma_beta_grad");
    check_tensor(input, "moreh_groupnorm_backward_gamma_beta_grad");
    check_tensor(mean, "moreh_groupnorm_backward_gamma_beta_grad");
    check_tensor(rstd, "moreh_groupnorm_backward_gamma_beta_grad");

    check_tensor(gamma_grad, "moreh_groupnorm_backward_gamma_beta_grad");
    check_tensor(beta_grad, "moreh_groupnorm_backward_gamma_beta_grad");

    // output_grad (N, C, H, W)
    auto C = output_grad.shape()[1];
    TT_ASSERT(C % this->num_groups == 0, "output_grad_shape[1] must be divisible by num_groups.");
    // input (N, C, H, W)
    C = input.shape()[1];
    TT_ASSERT(C % this->num_groups == 0, "input_shape[1] must be divisible by num_groups.");
    // gamma_grad (1, 1, 1, C)
    if (gamma_grad.has_value()) {
        C = gamma_grad.value().shape().without_padding()[-1];
        TT_ASSERT(C % this->num_groups == 0, "gamma_grad_shape[-1] must be divisible by num_groups.");
    }
    // beta_grad (1, 1, 1, C)
    if (beta_grad.has_value()) {
        C = beta_grad.value().shape().without_padding()[-1];
        TT_ASSERT(C % this->num_groups == 0, "beta_grad_shape[-1] must be divisible by num_groups.");
    }

    // mean (1, 1, N, num_groups)
    TT_ASSERT(mean.shape().without_padding()[-1] == this->num_groups, "mean_shape[-1] must match num_groups.");
    // rstd (1, 1, N, num_groups)
    TT_ASSERT(rstd.shape().without_padding()[-1] == this->num_groups, "rstd_shape[-1] must match num_groups.");
}

std::vector<Shape> MorehGroupNormBackwardGammaBetaGrad::compute_output_shapes(
    const std::vector<Tensor> &input_tensors) const {
    const auto &output_grad = input_tensors.at(0);
    // output_grad (N, C, H, W)
    const auto &output_grad_shape = output_grad.shape();

    // gamma_grad, beta_grad (1, 1, 1, C)
    auto dgamma_dbeta_origin_shape = output_grad_shape;
    const auto c = dgamma_dbeta_origin_shape[1];
    dgamma_dbeta_origin_shape[0] = 1;
    dgamma_dbeta_origin_shape[1] = 1;
    dgamma_dbeta_origin_shape[2] = TILE_HEIGHT;
    dgamma_dbeta_origin_shape[3] = TILE_WIDTH * ((c + TILE_WIDTH - 1) / TILE_WIDTH);

    auto dgamma_dbeta_padding = output_grad_shape.padding();
    dgamma_dbeta_padding[2] = Padding::PadDimension{0, TILE_HEIGHT - 1};
    dgamma_dbeta_padding[3] = Padding::PadDimension{0, TILE_WIDTH - (c % TILE_WIDTH)};

    Shape dgamma_dbeta_shape(dgamma_dbeta_origin_shape, dgamma_dbeta_padding);
    return {dgamma_dbeta_shape, dgamma_dbeta_shape};
}

std::vector<Tensor> MorehGroupNormBackwardGammaBetaGrad::create_output_tensors(
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
        result.push_back(create_device_tensor(output_shapes.at(0), dtype, layout, device, this->gamma_grad_mem_config));
    }

    if (output_tensors.at(1).has_value()) {
        result.push_back(output_tensors.at(1).value());
    } else {
        result.push_back(create_device_tensor(output_shapes.at(1), dtype, layout, device, this->beta_grad_mem_config));
    }

    return std::move(result);
}

operation::ProgramWithCallbacks MorehGroupNormBackwardGammaBetaGrad::create_program(
    const std::vector<Tensor> &input_tensors, std::vector<Tensor> &output_tensors) const {
    const auto &output_grad = input_tensors.at(0);
    const auto &input = input_tensors.at(1);
    const auto &mean = input_tensors.at(2);
    const auto &rstd = input_tensors.at(3);

    auto &gamma_grad = output_tensors.at(0);
    auto &beta_grad = output_tensors.at(1);

    return moreh_groupnorm_backward_gamma_beta_grad_impl(
        output_grad, input, mean, rstd, this->num_groups, gamma_grad, beta_grad);
}

std::vector<Tensor> moreh_groupnorm_backward_gamma_beta_grad(
    const Tensor &output_grad,
    const Tensor &input,
    const Tensor &mean,
    const Tensor &rstd,
    uint32_t num_groups,
    const std::optional<const Tensor> gamma_grad,
    const std::optional<const Tensor> beta_grad,
    const MemoryConfig &gamma_grad_mem_config,
    const MemoryConfig &beta_grad_mem_config) {
    return operation::run(
        MorehGroupNormBackwardGammaBetaGrad{
            .num_groups = num_groups,
            .gamma_grad_mem_config = std::move(gamma_grad_mem_config),
            .beta_grad_mem_config = std::move(beta_grad_mem_config)},
        {output_grad, input, mean, rstd},
        {},
        {gamma_grad, beta_grad});
}

std::vector<Tensor> moreh_groupnorm_backward(
    const Tensor &output_grad,
    const Tensor &input,
    const Tensor &mean,
    const Tensor &rstd,
    uint32_t num_groups,
    const std::optional<const Tensor> gamma,
    const std::optional<const Tensor> input_grad,
    const std::optional<const Tensor> gamma_grad,
    const std::optional<const Tensor> beta_grad,
    const MemoryConfig &input_grad_mem_config,
    const MemoryConfig &gamma_grad_mem_config,
    const MemoryConfig &beta_grad_mem_config) {
    const auto &dx = moreh_groupnorm_backward_input_grad(
        output_grad, input, mean, rstd, num_groups, gamma, input_grad, input_grad_mem_config);
    const auto &dgamma_dbeta = moreh_groupnorm_backward_gamma_beta_grad(
        output_grad, input, mean, rstd, num_groups, gamma_grad, beta_grad, gamma_grad_mem_config, beta_grad_mem_config);

    std::vector<Tensor> result;
    result.reserve(3);
    result.push_back(std::move(dx));

    result.push_back(std::move(dgamma_dbeta[0]));
    result.push_back(std::move(dgamma_dbeta[1]));

    return std::move(result);
}

}  // namespace primary

}  // namespace operations

}  // namespace tt

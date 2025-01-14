// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

#include "tensor/tensor.hpp"
#include "tt_dnn/op_library/operation.hpp"

namespace tt::tt_metal {
struct Fold {
    uint8_t stride_h;
    uint8_t stride_w;

    void validate(const std::vector<Tensor> &input_tensors) const;

    std::vector<Shape> compute_output_shapes(const std::vector<Tensor> &input_tensors) const;

    std::vector<Tensor> create_output_tensors(const std::vector<Tensor> &input_tensors) const;

    operation::ProgramWithCallbacks create_program(
        const std::vector<Tensor> &input_tensors, std::vector<Tensor> &output_tensors) const;

    static constexpr auto attribute_names = std::make_tuple("stride_h", "stride_w");

    const auto attribute_values() const { return std::make_tuple(stride_h, stride_w); }
};

operation::ProgramWithCallbacks fold_single_core(
    const Tensor &input, const Tensor &output, uint8_t stride_h, uint8_t stride_w);

Tensor fold(const Tensor &input_tensor_a, uint8_t stride_h, uint8_t stride_w);
}  // namespace tt::tt_metal

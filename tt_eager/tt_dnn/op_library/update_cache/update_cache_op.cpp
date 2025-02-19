// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_dnn/op_library/update_cache/update_cache_op.hpp"

#include "tt_metal/common/constants.hpp"
#include "tt_metal/host_api.hpp"

#include "third_party/magic_enum/magic_enum.hpp"

using namespace tt::constants;

namespace tt {

namespace tt_metal {


void UpdateCache::validate(const std::vector<Tensor>& input_tensors) const {
    const auto& cache_tensor = input_tensors.at(0);
    const auto& input_tensor = input_tensors.at(1);
    TT_FATAL(input_tensor.storage_type() == StorageType::DEVICE and cache_tensor.storage_type() == StorageType::DEVICE, "Operands to update_cache need to be on device!");
    TT_FATAL(input_tensor.device() == cache_tensor.device(), "Operands to update_cache need to be on the same device!");
    TT_FATAL(input_tensor.buffer() != nullptr and cache_tensor.buffer() != nullptr, "Operands to update_cache need to be allocated in buffers on device!");
    TT_FATAL((input_tensor.layout() == Layout::TILE && cache_tensor.layout() == Layout::TILE), "Inputs to update_cache must be tilized");
    TT_FATAL(input_tensor.dtype() == DataType::BFLOAT16 || input_tensor.dtype() == DataType::BFLOAT8_B);
    TT_FATAL(cache_tensor.dtype() == DataType::BFLOAT16 || cache_tensor.dtype() == DataType::BFLOAT8_B);

    TT_FATAL(input_tensor.shape()[-1] == cache_tensor.shape()[-1]);
    TT_FATAL(input_tensor.shape()[0] == 1);
    TT_FATAL(input_tensor.shape()[1] == cache_tensor.shape()[1]);
    TT_FATAL(cache_tensor.memory_config().memory_layout == TensorMemoryLayout::INTERLEAVED);
    if (this->op_type == UpdateCacheOpType::FILL) {
        // TODO: If we want to support mixed precision like decode, we need to add simple compute kernel for conversion
        TT_FATAL(input_tensor.dtype() == cache_tensor.dtype(), "Input and cache tensors must have same dtype!");

        // TODO: For interleaved, assume each core handles 1 tile of seq_len if kv_heads > 1
        // For 56 cores and 2 heads, this effectively caps max seq len at 56 / 2 * 32 = 896
        // Can generalize interleaved to infer and check arbitrary number of tiles along seq_len per core; or, add more robust logic in reader/writer loops to handle generic blocking of work
        // For sharded, we infer number of tiles each core handles from shard so no issues there
        if (input_tensor.memory_config().memory_layout == TensorMemoryLayout::INTERLEAVED and input_tensor.shape()[1] > 1) {
            const uint32_t num_blocks_of_work = input_tensor.shape()[1] * input_tensor.shape()[-2] / TILE_HEIGHT;
            const auto compute_with_storage_grid_size = input_tensor.device()->compute_with_storage_grid_size();
            TT_FATAL((num_blocks_of_work <= compute_with_storage_grid_size.x * compute_with_storage_grid_size.y));
        }

        if (input_tensor.is_sharded()) {
            TT_FATAL(input_tensor.memory_config().memory_layout != TensorMemoryLayout::WIDTH_SHARDED);
            TT_FATAL(input_tensor.shard_spec().value().shape[1] == input_tensor.shape()[-1]);
            // Require even work division along seq_len and also only 1 head per core
            TT_FATAL(input_tensor.shape()[-2] % input_tensor.shard_spec().value().shape[0] == 0, "Seq len must be divisible by shard height!");
        }

        TT_FATAL(this->batch_idx < cache_tensor.shape()[0]);
        TT_FATAL(input_tensor.shape()[-2] <= cache_tensor.shape()[-2]);
    } else if (this->op_type == UpdateCacheOpType::UPDATE) {
        if (input_tensor.is_sharded()) {
            TT_FATAL(input_tensor.memory_config().memory_layout != TensorMemoryLayout::WIDTH_SHARDED);
            TT_FATAL(input_tensor.shard_spec().value().shape[1] == input_tensor.shape()[-1]);
            // Require even work division for now
            TT_FATAL((input_tensor.volume() / input_tensor.shape()[-1]) % input_tensor.shard_spec().value().shape[0] == 0);
        } else {
            TT_FATAL(input_tensor.memory_config().memory_layout == TensorMemoryLayout::INTERLEAVED);
        }
        TT_FATAL(cache_tensor.shape()[0] == input_tensor.shape()[-2]);
    }
}

std::vector<Shape> UpdateCache::compute_output_shapes(const std::vector<Tensor>& input_tensors) const {
    // Do nothing because it's an in-place operation
    return {};
}

std::vector<Tensor> UpdateCache::create_output_tensors(const std::vector<Tensor>& input_tensors) const {
    // Do nothing because it's an in-place operation
    return {};
}

operation::ProgramWithCallbacks UpdateCache::create_program(const std::vector<Tensor>& input_tensors, std::vector<Tensor> &output_tensors) const {
    const auto& cache_tensor = input_tensors.at(0);
    const auto& input_tensor = input_tensors.at(1);

    switch(this->get_parallelization_strategy(input_tensors)) {
        case UpdateCacheOpParallelizationStrategy::MULTI_CORE:
            if (this->op_type == UpdateCacheOpType::FILL) {
                return fill_cache_multi_core(cache_tensor, input_tensor, this->batch_idx, this->update_idx);
            } else {
                return update_cache_multi_core(cache_tensor, input_tensor, this->update_idx);
            }
        case UpdateCacheOpParallelizationStrategy::SINGLE_CORE:
        default:
            if (this->op_type == UpdateCacheOpType::FILL) {
                return fill_cache_single_core(cache_tensor, input_tensor, this->batch_idx, this->update_idx);
            } else {
                return update_cache_single_core(cache_tensor, input_tensor, this->update_idx);
            }
    };
    return {};
}


UpdateCacheOpParallelizationStrategy UpdateCache::get_parallelization_strategy(const std::vector<Tensor> &input_tensors) const {
    const auto& input_tensor = input_tensors.at(1);
    if (this->op_type == UpdateCacheOpType::FILL) {
        // TODO: Deprecate SINGLE_CORE? Is there a reason to keep it around or can it just be handled by MULTI_CORE version
        return UpdateCacheOpParallelizationStrategy::MULTI_CORE;
    } else {
        uint32_t num_batch_heads = input_tensor.shape()[1] * input_tensor.shape()[-2] / TILE_HEIGHT;
        if (num_batch_heads > 1 || input_tensor.is_sharded()) {
            return UpdateCacheOpParallelizationStrategy::MULTI_CORE;
        }
        else{
            return UpdateCacheOpParallelizationStrategy::SINGLE_CORE;
        }
    }
}

tt::stl::reflection::Attributes UpdateCache::attributes() const {
    return {
        {"batch_idx", this->batch_idx},
        {"update_idx", this->update_idx},
        {"op_type", this->op_type},
    };
}

const operation::Hash UpdateCache::compute_program_hash(
    const std::vector<Tensor> &input_tensors) const {
    return operation::hash_operation<UpdateCache>(this->op_type, input_tensors);
}

}  // namespace tt_metal

}  // namespace tt

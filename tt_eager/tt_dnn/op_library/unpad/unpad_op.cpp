#include "tt_dnn/op_library/unpad/unpad_op.hpp"
#include "tt_dnn/op_library/math.hpp"
#include "tensor/tensor_utils.hpp"

#include "tt_metal/host_api.hpp"
#include "tt_metal/common/constants.hpp"
#include "tt_metal/detail/util.hpp"

using namespace tt::constants;

namespace tt {

namespace tt_metal {


void Unpad::validate(const std::vector<Tensor> &input_tensors) const {
    const auto& input_tensor_a = input_tensors.at(0);
    TT_ASSERT(input_tensor_a.storage_type() == StorageType::DEVICE, "Operands to unpad need to be on device!");
    TT_ASSERT(input_tensor_a.buffer() != nullptr , "Operands to unpad need to be allocated in buffers on device!");
    TT_ASSERT(input_tensor_a.layout() == Layout::TILE || input_tensor_a.layout() == Layout::ROW_MAJOR);

    TT_ASSERT(
        (this->output_tensor_start[0] == 0 && this->output_tensor_start[1] == 0 && output_tensor_start[2] == 0 && output_tensor_start[3] == 0),
        "On device unpadding only supports unpadding at end of dims"
    );

    TT_ASSERT(this->output_tensor_start[0] < input_tensor_a.shape()[0]);
    TT_ASSERT(this->output_tensor_end[0] < input_tensor_a.shape()[0]);
    TT_ASSERT(this->output_tensor_start[1] < input_tensor_a.shape()[1]);
    TT_ASSERT(this->output_tensor_end[1] < input_tensor_a.shape()[1]);
    TT_ASSERT(this->output_tensor_start[2] < input_tensor_a.shape()[2]);
    TT_ASSERT(this->output_tensor_end[2] < input_tensor_a.shape()[2]);
    TT_ASSERT(this->output_tensor_start[3] < input_tensor_a.shape()[3]);
    TT_ASSERT(this->output_tensor_end[3] < input_tensor_a.shape()[3]);

    // Check if start shape is <= end shape
    TT_ASSERT(this->output_tensor_start[0] <= this->output_tensor_end[0]);
    TT_ASSERT(this->output_tensor_start[1] <= this->output_tensor_end[1]);
    TT_ASSERT(this->output_tensor_start[2] <= this->output_tensor_end[2]);
    TT_ASSERT(this->output_tensor_start[3] <= this->output_tensor_end[3]);

    Shape output_tensor_shape = {
        this->output_tensor_end[0] - this->output_tensor_start[0] + 1,
        this->output_tensor_end[1] - this->output_tensor_start[1] + 1,
        this->output_tensor_end[2] - this->output_tensor_start[2] + 1,
        this->output_tensor_end[3] - this->output_tensor_start[3] + 1,
    };

    if (input_tensor_a.layout() == Layout::TILE) {
        TT_ASSERT(input_tensor_a.volume() % TILE_HW == 0);
        TT_ASSERT((output_tensor_shape[2] % TILE_HEIGHT == 0), "Can only unpad tilized tensor with full tiles");
        TT_ASSERT((output_tensor_shape[3] % TILE_WIDTH == 0), "Can only unpad tilized tensor with full tiles");
    } else if (input_tensor_a.layout() == Layout::ROW_MAJOR) {
        TT_ASSERT(output_tensor_shape[3] % 2 == 0, "RM unpadding requires output X dim to be a multiple of 2");
    }
}
std::vector<Shape> Unpad::compute_output_shapes(const std::vector<Tensor> &input_tensors) const {
    Shape output_tensor_shape = {
        this->output_tensor_end[0] - this->output_tensor_start[0] + 1,
        this->output_tensor_end[1] - this->output_tensor_start[1] + 1,
        this->output_tensor_end[2] - this->output_tensor_start[2] + 1,
        this->output_tensor_end[3] - this->output_tensor_start[3] + 1,
    };
    return {output_tensor_shape};
}
std::vector<Tensor> Unpad::create_output_tensors(const std::vector<Tensor> &input_tensors) const {
    const auto& input_tensor_a = input_tensors.at(0);
    return operation::generic_create_output_tensors(*this, input_tensors, input_tensor_a.dtype(), input_tensor_a.layout(), this->output_mem_config);
}

// TODO: If unpad is called on a tile and output is not tile, we could untilize then unpad, and output is RM
// Currently calling unpad on a tile requires the output unpad shape to be tile
operation::ProgramWithCallbacks Unpad::create_program(const std::vector<Tensor>& input_tensors, std::vector<Tensor> &output_tensors) const {
    const auto& input_tensor_a = input_tensors.at(0);
    auto& output_tensor = output_tensors.at(0);
    if (input_tensor_a.layout() == Layout::ROW_MAJOR) {
        return unpad_rm_single_core(input_tensor_a, output_tensor, output_tensor_start, output_tensor_end);
    } else if (input_tensor_a.layout() == Layout::TILE) {
        switch(this->get_parallelization_strategy(input_tensors)) {
            case UnpadOpParallelizationStrategy::MULTI_CORE: return unpad_tile_multi_core(input_tensor_a, output_tensor, output_tensor_start, output_tensor_end);
            case UnpadOpParallelizationStrategy::SINGLE_CORE:
            default:
                return unpad_tile_single_core(input_tensor_a, output_tensor, output_tensor_start, output_tensor_end);
        }
    } else {
        TT_ASSERT(false, "Unsupported layout for unpad");
        return {};
    }
}

UnpadOpParallelizationStrategy Unpad::get_parallelization_strategy(const std::vector<Tensor> &input_tensors) const {
    const auto& input_tensor_a = input_tensors.at(0);
    if (input_tensor_a.layout() == Layout::TILE) {
        auto shape = this->compute_output_shapes(input_tensors).at(0);
        auto num_tiles = tt::tt_metal::compute_volume(shape) / TILE_HW;
        if (num_tiles > 1) {
            return UnpadOpParallelizationStrategy::MULTI_CORE;
        } else {
            return UnpadOpParallelizationStrategy::SINGLE_CORE;
        }
    } else {
        return UnpadOpParallelizationStrategy::SINGLE_CORE;
    }
}

tt::stl::reflection::Attributes Unpad::attributes() const {
    return {
        {"output_tensor_start", this->output_tensor_start},
        {"output_tensor_end", this->output_tensor_end},
        {"output_mem_config", this->output_mem_config},
    };
}

Tensor unpad(const Tensor &input_tensor_a, const Shape &output_tensor_start, const Shape &output_tensor_end, const MemoryConfig& mem_config) {
    // No-op (Will do a tensor copy)
    // TODO: We need to run asserts before this
    const Shape output_tensor_shape = {
        output_tensor_end[0] - output_tensor_start[0] + 1,
        output_tensor_end[1] - output_tensor_start[1] + 1,
        output_tensor_end[2] - output_tensor_start[2] + 1,
        output_tensor_end[3] - output_tensor_start[3] + 1,
    };
    if (input_tensor_a.shape() == output_tensor_shape) {
        return input_tensor_a;
    }
    return operation::run_without_autoformat(Unpad{output_tensor_start, output_tensor_end, mem_config}, {input_tensor_a}).at(0);

}

void UnpadOnHost::validate(const std::vector<Tensor> &input_tensors) const {
    const auto& input_tensor = input_tensors.at(0);
    TT_ASSERT(input_tensor.storage_type() == StorageType::OWNED);
    TT_ASSERT(input_tensor.layout() == Layout::ROW_MAJOR);

    TT_ASSERT(this->output_tensor_start[0] < input_tensor.shape()[0]);
    TT_ASSERT(this->output_tensor_end[0] < input_tensor.shape()[0]);
    TT_ASSERT(this->output_tensor_start[1] < input_tensor.shape()[1]);
    TT_ASSERT(this->output_tensor_end[1] < input_tensor.shape()[1]);
    TT_ASSERT(this->output_tensor_start[2] < input_tensor.shape()[2]);
    TT_ASSERT(this->output_tensor_end[2] < input_tensor.shape()[2]);
    TT_ASSERT(this->output_tensor_start[3] < input_tensor.shape()[3]);
    TT_ASSERT(this->output_tensor_end[3] < input_tensor.shape()[3]);

    // Check if start shape is <= end shape
    TT_ASSERT(this->output_tensor_start[0] <= this->output_tensor_end[0]);
    TT_ASSERT(this->output_tensor_start[1] <= this->output_tensor_end[1]);
    TT_ASSERT(this->output_tensor_start[2] <= this->output_tensor_end[2]);
    TT_ASSERT(this->output_tensor_start[3] <= this->output_tensor_end[3]);
}
std::vector<Shape> UnpadOnHost::compute_output_shapes(const std::vector<Tensor> &input_tensors) const {
    Shape output_tensor_shape = {
        this->output_tensor_end[0] - this->output_tensor_start[0] + 1,
        this->output_tensor_end[1] - this->output_tensor_start[1] + 1,
        this->output_tensor_end[2] - this->output_tensor_start[2] + 1,
        this->output_tensor_end[3] - this->output_tensor_start[3] + 1,
    };
    return {output_tensor_shape};
}
std::vector<Tensor> UnpadOnHost::compute_output_tensors(const std::vector<Tensor>& input_tensors) const {
    const auto& input_tensor = input_tensors.at(0);
    if (input_tensor.shape() == UnpadOnHost::compute_output_shapes(input_tensors).at(0)) {
        return {input_tensor};
    } else {
        return {input_tensor.unpad(this->output_tensor_start, this->output_tensor_end)};
    }
}

tt::stl::reflection::Attributes UnpadOnHost::attributes() const {
    return {
        {"output_tensor_start", this->output_tensor_start},
        {"output_tensor_end", this->output_tensor_end},
    };
}

Tensor unpad_on_host(const Tensor &input_tensor, const Shape &output_tensor_start, const Shape &output_tensor_end, const MemoryConfig& mem_config) {
    return operation::run(UnpadOnHost{output_tensor_start, output_tensor_end}, {input_tensor}).at(0);
}

}  // namespace tt_metal

}  // namespace tt
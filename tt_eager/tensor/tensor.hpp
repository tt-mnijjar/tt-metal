// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <random>
#include <tuple>
#include <variant>
#include <vector>

#include "common/bfloat16.hpp"
#include "common/bfloat8.hpp"
#include "common/test_tiles.hpp"
#include "common/tt_backend_api_types.hpp"
#include "tensor/types.hpp"
#include "tt_metal/impl/buffers/buffer.hpp"
#include "tt_metal/impl/device/device.hpp"
#include "tt_metal/tt_stl/reflection.hpp"

namespace tt {

namespace tt_metal {

class Tensor {

    public:
        // ======================================================================================
        //                                  Hi Level APIs
        // ======================================================================================
        Tensor(const Storage &storage, const Shape &shape, DataType dtype, Layout layout);

        Tensor(const Tensor &other) = default;
        Tensor &operator=(const Tensor &other) = default;

        Tensor(Tensor &&other) = default;
        Tensor &operator=(Tensor &&other) = default;

        ~Tensor();

        void deallocate(bool force = false);

        Tensor to(
            Device *target_device,
            const MemoryConfig &mem_config = {.memory_layout = tt::tt_metal::TensorMemoryLayout::INTERLEAVED}) const;

        Tensor to(
            CommandQueue & queue,
            const MemoryConfig &mem_config = {.memory_layout = tt::tt_metal::TensorMemoryLayout::INTERLEAVED}) const;
        Tensor to(Layout target_layout) const;

        Tensor pad(const Shape &output_tensor_shape, const Shape &input_tensor_start, float pad_value) const;

        Tensor cpu ( CommandQueue & queue, bool blocking = true) const;
        Tensor cpu(bool blocking = true) const;

        Tensor cpu_sharded() const;

        Tensor unpad(const Shape &output_tensor_start, const Shape &output_tensor_end) const;

        Tensor pad_to_tile(float pad_value) const;

        Tensor unpad_from_tile(const Shape &output_tensor_shape) const;

        const std::string write_to_string() const;
        void print() const;

        Tensor extract_shard(const CoreCoord &core) const;
        Tensor extract_shard(const uint32_t &core_id) const;

        // ======================================================================================
        //                                  Low Level APIs
        // ======================================================================================
        Tensor reshape(int N, int C, int H, int W) const;
        Tensor reshape(const Shape &new_shape) const;

        // ======================================================================================
        //                                      Getters
        // ======================================================================================
        const Storage &storage() const;
        const Shape &shape() const { return this->shape_; }
        DataType dtype() const { return this->dtype_; }
        Layout layout() const { return this->layout_; }

        // ======================================================================================
        //                                      Extra Helper Functions
        // ======================================================================================

        StorageType storage_type() const;
        const Shape strides() const;
        uint32_t volume() const;

        bool is_allocated() const;

        bool is_contiguous() const {
            if (this->layout_ == tt::tt_metal::Layout::ROW_MAJOR) {
                return this->shape_ == this->shape_.without_padding();
            } else {
                return false;
            }
        }

        // TODO(arakhmati): clean up the methods below
        Buffer *buffer() const { return std::get<DeviceStorage>(this->storage_).buffer.get(); }
        DeviceBuffer device_buffer() const { return std::get<DeviceStorage>(this->storage_).buffer; }

        Device *device() const {
            if (this->storage_type() == tt::tt_metal::StorageType::DEVICE) {
                auto buffer = this->buffer();
                if (buffer == nullptr)
                    TT_THROW("Cannot get the device from a tensor without an allocated buffer");
                return buffer->device();
            } else {
                TT_THROW("Cannot get the device from a tensor with host storage");
            }
        }
        const MemoryConfig memory_config() const {
            return std::visit(
                [](const auto &storage) -> MemoryConfig {
                    using T = std::decay_t<decltype(storage)>;
                    if constexpr (std::is_same_v<T, DeviceStorage>) {
                        return storage.memory_config();
                    } else {
                        TT_THROW("MemoryConfig can only be obtained for a tensor with DeviceStorage");
                    }
                },
                this->storage_);
        }
        const std::optional<ShardSpec> shard_spec() const { return this->memory_config().shard_spec; }

        const bool is_sharded() const { return this->storage_type() == StorageType::DEVICE ? this->memory_config().is_sharded() : false; }

        // Size in bytes of a single element held in tensor
        uint32_t element_size() const;

        static constexpr auto attribute_names = std::make_tuple("storage", "shape", "dtype", "layout");
        const auto attribute_values() const {
            return std::make_tuple(
                std::cref(this->storage_), std::cref(this->shape_), std::cref(this->dtype_), std::cref(this->layout_));
        }

        std::vector<uint32_t> host_page_ordering();

        const ttnn::Shape ttnn_shape() const { return ttnn::Shape(this->shape_); }

    private:
     Storage storage_;
     Shape shape_;
     DataType dtype_;
     Layout layout_;
};



Tensor create_device_tensor(const Shape& shape, DataType dtype, Layout layout, Device *device, const MemoryConfig& memory_config = {.memory_layout=tt::tt_metal::TensorMemoryLayout::INTERLEAVED});

Tensor create_sharded_device_tensor(const Shape& shape, DataType data_type, Layout layout, Device *device, const MemoryConfig& memory_config);

// template<typename Buffer>
// void *get_host_buffer(const Tensor &tensor);
void *get_raw_host_data_ptr(const Tensor &tensor);

void memcpy(
    CommandQueue &queue, void *dst, const Tensor &src, const std::optional<std::size_t> transfer_size = std::nullopt);
void memcpy(
    CommandQueue &queue, Tensor &dst, const void *src, const std::optional<std::size_t> transfer_size = std::nullopt);
void memcpy(
    CommandQueue &queue, Tensor &dst, const Tensor &src, const std::optional<std::size_t> transfer_size = std::nullopt);

void memcpy(void *dst, const Tensor &src, const std::optional<std::size_t> transfer_size = std::nullopt);
void memcpy(Tensor &dst, const void *src, const std::optional<std::size_t> transfer_size = std::nullopt);
void memcpy(Tensor &dst, const Tensor &src, const std::optional<std::size_t> transfer_size = std::nullopt);

}  // namespace tt_metal

}  // namespace tt

namespace ttnn {

using Tensor = tt::tt_metal::Tensor;

}  // namespace ttnn

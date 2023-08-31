#pragma once

#include <vector>
#include <array>
#include <random>
#include <tuple>

#include "tensor/types.hpp"
#include "tt_metal/impl/device/device.hpp"
#include "tt_metal/impl/buffers/buffer.hpp"
#include "common/test_tiles.hpp"
#include "common/tt_backend_api_types.hpp"
#include "common/bfloat16.hpp"
#include "common/bfloat8.hpp"

#include "tt_stl/reflection.hpp"

namespace tt {

namespace tt_metal {

class Tensor {

    public:
        // ======================================================================================
        //                                  Hi Level APIs
        // ======================================================================================
        Tensor(const Storage& storage, const Shape& shape, DataType dtype, Layout layout);

        Tensor(const Tensor &other) = default;
        Tensor& operator=(const Tensor &other) = default;

        Tensor(Tensor &&other) = default;
        Tensor& operator=(Tensor &&other) = default;

        ~Tensor();

        void deallocate();

        Tensor to(Device *target_device, const MemoryConfig &mem_config={.interleaved=true}) const;

        Tensor to(Layout target_layout) const;

        Tensor pad(const Shape &output_tensor_shape, const Shape &input_tensor_start, float pad_value) const;

        Tensor cpu() const;

        Tensor unpad(const Shape &output_tensor_start, const Shape &output_tensor_end) const;

        Tensor pad_to_tile(float pad_value) const;

        Tensor unpad_from_tile(const Shape &output_tensor_shape) const;

        void print(Layout print_layout=Layout::ROW_MAJOR, bool pretty_print=false) const;

        // Prints like numpy print function to make it more readable. Only supports row major layout.
        void pretty_print() const;

        // ======================================================================================
        //                                  Low Level APIs
        // ======================================================================================
        Tensor reshape(int N, int C, int H, int W);
        Tensor reshape(const Shape& new_shape) const;

        // ======================================================================================
        //                                      Getters
        // ======================================================================================
        const Storage& storage() const;
        const Shape& shape() const { return this->shape_; }
        DataType dtype() const { return this->dtype_; }
        Layout layout() const { return this->layout_; }

        // ======================================================================================
        //                                      Extra Helper Functions
        // ======================================================================================

        StorageType storage_type() const;
        const Shape strides() const;
        uint32_t volume() const;

        bool is_allocated() const;

        // TODO(arakhmati): clean up the methods below
        Buffer* buffer() const { return std::get<DeviceStorage>(this->storage_).buffer.get(); }
        Device* device() const { return std::get<DeviceStorage>(this->storage_).device; }
        const MemoryConfig memory_config() const { return std::get<DeviceStorage>(this->storage_).memory_config; };

        // Size in bytes of a single element held in tensor
        uint32_t element_size() const;

        tt::stl::reflection::Attributes attributes() const;


    private:
        Storage storage_;
        Shape shape_;
        DataType dtype_;
        Layout layout_;
};

Tensor create_device_tensor(const Shape& shape, DataType dtype, Layout layout, Device *device, const MemoryConfig& memory_config = {.interleaved=true});

}  // namespace tt_metal

}  // namespace tt
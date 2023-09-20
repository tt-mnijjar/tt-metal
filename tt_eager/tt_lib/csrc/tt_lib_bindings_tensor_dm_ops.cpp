// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_lib_bindings_tensor.hpp"
#include "tt_lib_bindings_tensor_impl.hpp"
#include "tt_dnn/op_library/move/move_op.hpp"
#include "tt_dnn/op_library/tilize/tilize_op.hpp"
#include "tt_dnn/op_library/untilize/untilize_op.hpp"
#include "tt_dnn/op_library/reshape/reshape_op.hpp"
#include "tt_dnn/op_library/permute/permute_op.hpp"
#include "tt_dnn/op_library/pad/pad_op.hpp"
#include "tt_dnn/op_library/unpad/unpad_op.hpp"
#include "tt_dnn/op_library/transpose/transpose_op.hpp"
#include "tt_dnn/op_library/fill_rm/fill_rm_op.hpp"
#include "tt_dnn/op_library/concat/concat_op.hpp"
#include "tt_dnn/op_library/bcast/bcast_op.hpp"
#include "tt_dnn/op_library/reduce/reduce_op.hpp"
#include "tt_dnn/op_library/clone/clone_op.hpp"

namespace tt::tt_metal::detail{

    void TensorModuleDMOPs( py::module & m_tensor)
    {

        // reduce enums
        detail::export_enum<ReduceOpMath>(m_tensor);

        detail::export_enum<ReduceOpDim>(m_tensor);

        // bcast enums
        detail::export_enum<BcastOpMath>(m_tensor);
        /** TODO: add these to bcast ops - good to have not required
            .value("GT", BcastOpMath::GT)
            .value("LT", BcastOpMath::LT)
            .value("GE", BcastOpMath::GE)
            .value("LE", BcastOpMath::LE)
            .value("EQ", BcastOpMath::EQ)
            .value("NEQ", BcastOpMath::NE);
        */

        detail::export_enum<BcastOpDim>(m_tensor);

        detail::bind_unary_op(m_tensor, "clone", &clone, R"doc(  Returns a new tensor which is a new copy of input tensor ``{0}``.)doc");

        // *** tensor manipulation ***
        m_tensor.def("concat", &concat,
            py::arg("input_tensors").noconvert(), py::arg("dim") = 0, py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Concatenates shape of tensors ``arg0`` and ``arg1`` to new shape ``[W, Z, Y, X]`` along the specified dimension ``arg1``.

            Input tensors must be on device, in ROW MAJOR or TILE layout, and have matching data type.

            Output tensor will be on device, in same layout, and have same data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input_tensors", "Input tensors to concat", "List of Tensors", "Tensors of shape [W, Z, Y, X], where Y or X must be a multiple of 32 if they are the concat dim", "Yes"
                "dim", "dimension of concat", "int", "", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("reshape", &reshape,
            py::arg("input").noconvert(), py::arg("W"), py::arg("Z"), py::arg("Y"), py::arg("X"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Returns a tensor with the new shape of ``[W, Z, Y, X]``. The X dimension of input and output tensor must have same size.

            Input tensor must be on host device, in TILE layout, and have BFLOAT16 data type.

            Output tensor will be on host device, in TILE layout, and have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X]", "Yes"
                "W", "W dim of output tensor", "int", "", "Yes"
                "Z", "Z dim of output tensor", "int", "", "Yes"
                "Y", "Y dim of output tensor", "int", "", "Yes"
                "X", "X dim of output tensor", "int", "", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("permute", &permute,
            py::arg("input").noconvert(), py::arg("W"), py::arg("Z"), py::arg("Y"), py::arg("X"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Returns a tensor that is input tensor ``arg0`` with its dimensions permuted to new order ``[arg1, arg2, arg3, arg4]``.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X]", "Yes"
                "W", "Dim to become W", "int", "Unique value between [0, num dims)", "Yes"
                "Z", "Dim to become Z", "int", "Unique value between [0, num dims)", "Yes"
                "Y", "Dim to become Y", "int", "Unique value between [0, num dims)", "Yes"
                "X", "Dim to become X", "int", "Unique value between [0, num dims)", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("tilize", &tilize,
            py::arg("input").noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Changes data layout of input tensor to TILE.

            Input tensor must be on TT accelerator device, in ROW_MAJOR layout, and have BFLOAT16 data type.

            Output tensor will be on TT accelerator device, in TILE layout, and have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X] where Y%32=0 and X%32=0", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("tilize_with_zero_padding", &tilize_with_zero_padding,
            py::arg("input").noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Tilizes a given tensor across memory on device. Pads zeroes height-wise and width-wise if required.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X]", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("tilize_with_val_padding",
            [] (const Tensor &tensor, const std::array<uint32_t, 4> &output_tensor_shape, const std::array<uint32_t, 4> &input_tensor_start, float pad_value, const MemoryConfig& output_mem_config) {
                return tilize_with_val_padding(tensor, output_tensor_shape, input_tensor_start, pad_value, output_mem_config);
            },
            py::arg("input").noconvert(), py::arg("output_tensor_shape").noconvert(), py::arg("input_tensor_start"), py::arg("pad_value"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Tilizes a given tensor across memory on device. Pads to specified shape before tilizing.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "", "Yes"
                "output_tensor_shape", "Shape of output tensor", "List[int[4]]", "Shape [W, Z, Y, X] where Y%32=0 and X%32=0", "Yes"
                "input_tensor_start", "Start indices to place input tensor in output tensor", "List[int[4]]", "Must be all 0s", "Yes"
                "pad_value", "Value to pad input tensor", "float", "", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("untilize", &untilize,
            py::arg("input").noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Changes data layout of input tensor to ROW_MAJOR.

            Input tensor must be on TT accelerator device, in TILE, and have BFLOAT16 data type.

            Output tensor will be on TT accelerator device, in ROW_MAJOR layout, and have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X] where Y%32=0 and X%32=0", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("untilize_with_unpadding",
            [] (const Tensor &tensor, const std::array<uint32_t, 4> &output_tensor_shape, const std::array<uint32_t, 4> &input_tensor_start, const MemoryConfig& output_mem_config) {
                return untilize_with_unpadding(tensor, output_tensor_shape, input_tensor_start, output_mem_config);
            },
            py::arg("input").noconvert(), py::arg("output_tensor_start").noconvert(), py::arg("output_tensor_end"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Changes data layout of input tensor to ROW_MAJOR and unpads/removes elements from the tensor.

            Input tensor must be on TT accelerator device, in TILE, and have BFLOAT16 data type.

            Output tensor will be on TT accelerator device, in ROW_MAJOR layout, and have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X] where Y%32=0 and X%32=0", "Yes"
                "output_tensor_start", "Start indices of input tensor", "List[int[4]]", "Must be all 0s", "Yes"
                "output_tensor_end", "End indices of input tensor in output tensor", "List[int[4]]", "Values along each dim must be < input_tensor_shape[i]", "Yes"
                "pad_value", "Value to pad input tensor", "float", "", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("pad",
            [] (const Tensor &input_tensor, const std::array<uint32_t, 4> &output_tensor_shape, const std::array<uint32_t, 4> &input_tensor_start, float pad_value, const MemoryConfig& output_mem_config) {
                return pad(input_tensor, output_tensor_shape, input_tensor_start, pad_value, output_mem_config);
            },
            py::arg("input").noconvert(), py::arg("output_tensor_shape").noconvert(), py::arg("input_tensor_start"), py::arg("pad_value"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Pad TT Tensor with given pad value ``arg2``.

            The input tensor must be in ROW_MAJOR or TILE layout.

            Returns an output tensor that contains the input tensor at the given input tensor start indices ``arg3`` and the padded value everywhere else.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "", "Yes"
                "output_tensor_shape", "Shape of output tensor", "List[int[4]]", "", "Yes"
                "input_tensor_start", "Start indices to place input tensor in output tensor", "List[int[4]]", "Must be all 0s", "Yes"
                "pad_value", "Value to pad input tensor", "float", "", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("unpad",
            [] (const Tensor &input_tensor, const std::array<uint32_t, 4> &output_tensor_start, const std::array<uint32_t, 4> &output_tensor_end, const MemoryConfig& output_mem_config) {
                return unpad(input_tensor, output_tensor_start, output_tensor_end, output_mem_config);
            },
            py::arg("input").noconvert(), py::arg("output_tensor_start").noconvert(), py::arg("output_tensor_end"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Unpad TT Tensor.

            Returns an output tensor from output tensor start indices ``arg1`` to output tensor end indices ``arg2`` (inclusive) of the input tensor.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "", "Yes"
                "output_tensor_start", "Start indices of input tensor", "List[int[4]]", "Must be all 0s", "Yes"
                "output_tensor_end", "End indices of input tensor in output tensor", "List[int[4]]", "Values along each dim must be < input_tensor_shape[i]", "Yes"
                "pad_value", "Value to pad input tensor", "float", "", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        // *** broadcast and reduce ***
        m_tensor.def("bcast", &bcast,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("math_op"), py::arg("dim"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Perform a binary elementwise operation ``math_op`` between tensors ``input`` and ``other``, where values from tensor ``other`` are broadcast.

            Let tensor ``input`` have shape ``[W0, Z0, Y0, X0]`` and tensor ``other`` shape ``[W1, Z1, Y1, X1]``. ``arg3`` determines the type of broadcast performed.

            For ``dim=BcastOpDim::W`` broadcast is performed on dimension ``X``. ``Y0`` and ``Y1`` must be the same and either (W1=1 and Z1=1) or (W0=W1 and Z0=Z1).

            For ``dim=BcastOpDim::H`` broadcast is performed on dimension  ``Y``. ``X0`` and ``X1`` must be the same and either (W1=1 and Z1=1) or (W0=W1 and Z0=Z1).

            For ``dim=BcastOpDim::HW`` broadcast is performed on dimensions ``X`` and ``Y``. Either (W1=1 and Z1=1) or (W0=W1 and Z0=Z1) must hold for input shapes.

            Both input tensors must have BFLOAT16 data type.

            Output tensor will have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W0, Z0, Y0, X0]", "Yes"
                "other", "Input tensor to broadcast", "Tensor", "Tensor of shape [W1, Z1, Y1, X1]", "Yes"
                "math_op", "Aggregating math operation", " BcastOpMath", "ADD, SUB, MUL", "Yes"
                "dim", "Dimension on which to broadcast", "BcastOpDim", "W, H, HW", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("bcast_without_autoformat", &bcast_without_autoformat,
            py::arg().noconvert(), py::arg().noconvert(), py::arg("math_op"), py::arg("dim"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Perform a binary elementwise operation ``arg2`` between tensors ``arg0`` and ``arg1``, where values from tensor ``arg1`` are broadcast.

            Let tensor ``arg0`` have shape ``[W0, Z0, Y0, X0]`` and tensor ``arg1`` shape ``[W1, Z1, Y1, X1]``. ``arg3`` determines the type of broadcast performed.

            For ``arg3=BcastOpDim::W`` broadcast is performed on dimension ``X``. ``Y0`` and ``Y1`` must be the same and either (W1=1 and Z1=1) or (W0=W1 and Z0=Z1).

            For ``arg3=BcastOpDim::H`` broadcast is performed on dimension  ``Y``. ``X0`` and ``X1`` must be the same and either (W1=1 and Z1=1) or (W0=W1 and Z0=Z1).

            For ``arg3=BcastOpDim::HW`` broadcast is performed on dimensions ``X`` and ``Y``. Either (W1=1 and Z1=1) or (W0=W1 and Z0=Z1) must hold for input shapes.

            Both input tensors must have BFLOAT16 data type.

            Output tensor will have BFLOAT16 data type.

            Auto formatting is disabled. Input tensors must have TILE layout. Output tensors will have TILE layout.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W0, Z0, Y0, X0], where Y0%32=0 and X0%32=0", "Yes"
                "other", "Input tensor to broadcast", "Tensor", "Tensor of shape [W1, Z1, Y1, X1], where Y1%32=0 and X1%32=0", "Yes"
                "math_op", "Aggregating math operation", " BcastOpMath", "ADD, SUB, MUL", "Yes"
                "dim", "Dimension on which to broadcast", "BcastOpDim", "W, H, HW", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        m_tensor.def("reduce", &reduce,
            py::arg("input").noconvert(), py::arg("math_op"), py::arg("dim"), py::arg("scaler"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Perform a reduction of input tensor ``arg0`` using mathematical operation ``arg1`` on dimension ``arg2``.

            For ``arg2=ReduceOpDim::W`` reduce is done on dimension X.

            For ``arg2=ReduceOpDim::H`` reduce is done on dimension Y.

            For ``arg2=ReduceOpDim::HW`` reduce is done on dimensions X and Y.

            Input tensors must have BFLOAT16 data type.

            Output tensor will have BFLOAT16 data type.

            .. csv-table::
                :header: "Argument", "Description", "Data type", "Valid range", "Required"

                "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X]", "Yes"
                "math_op", "Aggregating math operation", " ReduceOpMath", "SUM, MAX", "Yes"
                "dim", "Dimension on which reduction is performed", "ReduceOpDim", "W, H, HW", "Yes"
                "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
        )doc");

        // *** experimental operations ***
        m_tensor.def("fill_rm", &fill_rm,
            py::arg("N"), py::arg("C"), py::arg("H"), py::arg("W"), py::arg("hOnes"), py::arg("wOnes"), py::arg("any").noconvert(), py::arg("val_hi"), py::arg("val_lo"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Generates an NCHW row-major tensor and fill it with high values up to
            hOnes, wOnes in each HW tile with the rest padded with high values. So
            for H=2, W=3, hFill=1, wFill=2 the following tensor will be generated:

            .. code-block::

                +------------> W
                | hi hi lo
                | lo lo lo
                |
                v H

            H, W are expected to be multiples of 32.

            The 'any' Tensor arg is only used to pass the device and resulting
            tensor dtype.

            val_hi/lo are expected to be floats.

            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | Argument | Description                                                           | Data type             | Valid range            | Required |
            +==========+=======================================================================+=======================+========================+==========+
            | N        | Batch count of output tensor                                          | int                   | N > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | C        | Channel count of output tensor                                        | int                   | C > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | H        | Height count of output tensor                                         | int                   | H > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | W        | Width count of output tensor                                          | int                   | W > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | hOnes    | Height of high values region                                          | int                   | hOnes <= H             | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | wOnes    | Width of high values region                                           | int                   | wOnes <= W             | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | any      | Any input tensor with desired device and data types for output tensor | tt_lib.tensor.Tensor  |                        | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | val_hi   | High value to use                                                     | float                 |                        | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | val_lo   | Low value to use                                                      | float                 |                        | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
        )doc");
        m_tensor.def("fill_ones_rm", &fill_ones_rm,
            py::arg("N"), py::arg("C"), py::arg("H"), py::arg("W"), py::arg("hOnes"), py::arg("wOnes"), py::arg("any").noconvert(), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
            Same as ``fill_rm``, but ``val_hi`` is set to ``1`` and ``val_lo`` is
            ``0``.

            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | Argument | Description                                                           | Data type             | Valid range            | Required |
            +==========+=======================================================================+=======================+========================+==========+
            | N        | Batch count of output tensor                                          | int                   | N > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | C        | Channel count of output tensor                                        | int                   | C > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | H        | Height count of output tensor                                         | int                   | H > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | W        | Width count of output tensor                                          | int                   | W > 0                  | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | hOnes    | Height of high values region                                          | int                   | hOnes <= H             | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | wOnes    | Width of high values region                                           | int                   | wOnes <= W             | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
            | any      | Any input tensor with desired device and data types for output tensor | tt_lib.tensor.Tensor  |                        | Yes      |
            +----------+-----------------------------------------------------------------------+-----------------------+------------------------+----------+
        )doc");

        m_tensor.def("move", &move,
            py::arg().noconvert(), py::arg("output_mem_config").noconvert() = std::nullopt, R"doc(
            Moves the elements of the input tensor ``arg0`` to a location in memory with specified memory layout.

            If no memory layout is specified, output memory will be the same as the input tensor memory config.

            +----------+----------------------------+----------------------------+---------------------------------+----------+
            | Argument | Description                | Data type                  | Valid range                     | Required |
            +==========+============================+============================+=================================+==========+
            | arg0     | Tensor to move             | Tensor                     | Tensor of shape [W, Z, Y, X]    | Yes      |
            +----------+----------------------------+----------------------------+---------------------------------+----------+
            | arg1     | MemoryConfig of tensor of  | tt_lib.tensor.MemoryConfig | Default is same as input tensor | No       |
            |          | TT accelerator device      |                            |                                 |          |
            +----------+----------------------------+----------------------------+---------------------------------+----------+
        )doc");

        m_tensor.def("transpose", py::overload_cast<const Tensor&, uint, uint, const MemoryConfig&>(&transpose),
        py::arg("input").noconvert(), py::arg("dim0"), py::arg("dim1"), py::arg("output_mem_config").noconvert() = operation::DEFAULT_OUTPUT_MEMORY_CONFIG, R"doc(
        Returns a tensor that is a transposed version of input tensor with shape ``[W, Z, Y, X]``, where dimensions ``arg1`` and ``arg2`` are swapped.

        Input tensor must have BFLOAT16 data type. Second and third input specify the dimensions of tensor to be transposed.

        Output tensor will have BFLOAT16 data type.

        .. csv-table::
            :header: "Argument", "Description", "Data type", "Valid range", "Required"

            "input", "Input tensor", "Tensor", "Tensor of shape [W, Z, Y, X]", "Yes"
            "dim0", "dimension to transpose", "uint", "0, 1, 2, or 3", "Yes"
            "dim1", "dimension to transpose", "uint", "0, 1, 2, or 3", "Yes"
            "output_mem_config", "Layout of tensor in TT Accelerator device memory banks", "MemoryConfig", "Default is interleaved in DRAM", "No"
       )doc");

        //transpose = transpose_wh
        detail::bind_unary_op(m_tensor, "transpose", py::overload_cast<const Tensor&, const MemoryConfig&>(&transpose), R"doc(Returns a tensor that is a transposed version of input tensor with shape ``[W, Z, Y, X]``, where dimensions ``X`` and ``Y`` are swapped.)doc");
        detail::bind_unary_op(m_tensor, "transpose_hc", &transpose_hc, R"doc(Returns a tensor that is a transposed version of input tensor with shape ``[W, Z, Y, X]``, where dimensions ``Y`` and ``Z`` are swapped.)doc");
        detail::bind_unary_op(m_tensor, "transpose_cn", &transpose_cn, R"doc(Returns a tensor that is a transposed version of input tensor with shape ``[W, Z, Y, X]``, where dimensions ``Z`` and ``W`` are swapped.)doc");
        detail::bind_unary_op(m_tensor, "transpose_nh", &transpose_nh, R"doc(Returns a tensor that is a transposed version of input tensor with shape ``[W, Z, Y, X]``, where dimensions ``W`` and ``Y`` are swapped.)doc");
        detail::bind_unary_op(m_tensor, "transpose_cw", &transpose_cw, R"doc(Returns a tensor that is a transposed version of input tensor with shape ``[W, Z, Y, X]``, where dimensions ``Z`` and ``X`` are swapped.)doc");
        detail::bind_unary_op(m_tensor, "transpose_nw", &transpose_nw, R"doc(Returns a tensor that is a transposed version of input tensor with shape ``[W, Z, Y, X]``, where dimensions ``W`` and ``X`` are swapped.)doc");

    }

}
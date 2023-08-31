#include <algorithm>
#include <cmath>

#include "tt_dnn/op_library/pool/max_pool.hpp"
#include "tt_dnn/op_library/reduce/reduce_op.hpp"   // for reduce_op_utils
#include "tt_metal/host_api.hpp"
#include "tensor/tensor_utils.hpp"
#include "detail/util.hpp"

#define DEBUG_SERVER 0

#if DEBUG_SERVER == 1
    #include "tt_metal/llrt/tt_debug_print_server.hpp"
#endif

uint32_t ceil_multiple_of(uint32_t n, uint32_t m) {
    return (uint32_t) ceil((float) n / m) * m;
}

namespace tt {
namespace tt_metal {

operation::ProgramWithCallbacks max_pool_2d_single_core(const Tensor &input, Tensor& output,
                                                        uint32_t in_h, uint32_t in_w,
                                                        uint32_t out_h, uint32_t out_w,
                                                        uint32_t kernel_size_h, uint32_t kernel_size_w,
                                                        uint32_t stride_h, uint32_t stride_w,
                                                        uint32_t pad_h, uint32_t pad_w,
                                                        uint32_t dilation_h, uint32_t dilation_w,
                                                        const MemoryConfig& out_mem_config,
                                                        uint32_t nblocks) {
    Program program = Program();
    CoreRange cores = {.start={0, 0}, .end={0, 0}};

    // This should allocate a DRAM buffer on the device
    Device *device = input.device();
    Buffer *src_dram_buffer = input.buffer();
    Buffer *dst_dram_buffer = output.buffer();

    Shape input_shape = input.shape();
    Shape output_shape = output.shape();

    log_debug("SHAPES: input = {}, output = {}", input_shape, output_shape);

    #if DEBUG_SERVER == 1
        // start debug server
        tt_start_debug_print_server(device->cluster(), {0}, {{1, 1}});
    #endif

    // NOTE: input is assumed to be in {N, 1, H * W, C }

    // TODO [AS]: Support other data formats??
    DataFormat in_df = datatype_to_dataformat_converter(input.dtype());
    DataFormat out_df = datatype_to_dataformat_converter(output.dtype());
    uint32_t in_nbytes = datum_size(in_df);
    uint32_t out_nbytes = datum_size(out_df);
    uint32_t in_nbytes_c = input_shape[3] * in_nbytes;      // row of input (channels)
    uint32_t out_nbytes_c = output_shape[3] * out_nbytes;   // row of output (channels)

    uint32_t kernel_size_hw = kernel_size_w * kernel_size_h;    // number of valid rows, to read
    uint32_t kernel_size_hw_padded = ceil_multiple_of(kernel_size_hw, constants::TILE_HEIGHT);
    uint32_t in_ntiles_hw = (uint32_t) ceil((float) kernel_size_hw_padded / constants::TILE_HEIGHT);
    uint32_t in_ntiles_c = (uint32_t) ceil((float) input_shape[3] / constants::TILE_WIDTH);
    uint32_t out_ntiles_hw = (uint32_t) ceil((float) output_shape[2] / constants::TILE_HEIGHT);
    uint32_t out_ntiles_c = (uint32_t) ceil((float) output_shape[3] / constants::TILE_WIDTH);

    uint32_t out_nelems = nblocks;     // TODO [AS]: Remove hard coding after identifying optimal param val
    uint32_t out_w_loop_count = ceil((float) out_w / out_nelems);

    // CBs
    uint32_t multi_buffering_factor = 2;

    uint32_t in_cb_id = CB::c_in0;          // input rows for "multiple (out_nelems)" output pixels
    uint32_t in_cb_page_nelems_padded = ceil_multiple_of(input_shape[3] * kernel_size_hw_padded, constants::TILE_HW);    // NOTE: ceil to tile size since triscs work with tilesize instead of pagesize
    uint32_t in_cb_pagesize = in_nbytes * in_cb_page_nelems_padded;
    uint32_t in_cb_npages = multi_buffering_factor * out_nelems;
    auto in_cb = CreateCircularBuffers(program,
                                       in_cb_id,
                                       cores,
                                       in_cb_npages,
                                       in_cb_npages * in_cb_pagesize,
                                       in_df);
    uint32_t in_scalar_cb_id = CB::c_in1;
    uint32_t in_scalar_cb_pagesize = tile_size(in_df);
    uint32_t in_scalar_cb_npages = 1;
    auto in_scalar_cb = CreateCircularBuffers(program,
                                              in_scalar_cb_id,
                                              cores,
                                              in_scalar_cb_npages,
                                              in_scalar_cb_npages * in_scalar_cb_pagesize,
                                              in_df);
    uint32_t in_tiled_cb_id = CB::c_intermed0;  // tiled input
    uint32_t in_tiled_cb_pagesize = tile_size(in_df);
    uint32_t in_tiled_cb_npages = in_ntiles_c * in_ntiles_hw * out_nelems;
    auto in_tiled_cb = CreateCircularBuffers(program,
                                             in_tiled_cb_id,
                                             cores,
                                             in_tiled_cb_npages,
                                             in_tiled_cb_npages * in_tiled_cb_pagesize,
                                             in_df);
    uint32_t out_tiled_cb_id = CB::c_intermed1; // tiled output
    uint32_t out_tiled_cb_pagesize = tile_size(out_df);
    uint32_t out_tiled_cb_npages = out_ntiles_c * out_nelems;
    auto out_tiled_cb = CreateCircularBuffers(program,
                                              out_tiled_cb_id,
                                              cores,
                                              out_tiled_cb_npages,
                                              out_tiled_cb_npages * out_tiled_cb_pagesize,
                                              out_df);
    uint32_t out_cb_id = CB::c_out0;            // output rows in RM
    uint32_t out_cb_pagesize = ceil_multiple_of(out_nbytes_c, constants::TILE_HW * out_nbytes);    // pad to tile size
    uint32_t out_cb_npages = multi_buffering_factor * out_nelems;    // there is just one row of channels after reduction
    auto cb_out = CreateCircularBuffers(program,
                                        out_cb_id,
                                        cores,
                                        out_cb_npages,
                                        out_cb_npages * out_cb_pagesize,
                                        out_df);

    /**
     * Reader Kernel: input rows -> input cb
     */
    float one = 1.;
    uint32_t bf16_one_u32 = *reinterpret_cast<uint32_t*>(&one);
    std::vector<uint32_t> reader_ct_args = {input.memory_config().buffer_type == BufferType::DRAM ? (uint) 1 : (uint) 0,
                                            out_mem_config.buffer_type == BufferType::DRAM ? (uint) 1 : (uint) 0,
                                            bf16_one_u32,
                                            out_nelems,
                                            static_cast<uint32_t>(((in_nbytes_c & (in_nbytes_c - 1)) == 0) ? 1 : 0)};  // in_nbytes_c is power of 2
    uint32_t in_log_base_2_of_page_size = (uint32_t) log2((float) in_nbytes_c);
    std::vector<uint32_t> reader_rt_args = {src_dram_buffer->address(),
                                            dst_dram_buffer->address(),
                                            kernel_size_h, kernel_size_w, kernel_size_hw, kernel_size_hw_padded,
                                            stride_h, stride_w,
                                            pad_h, pad_w,
                                            out_h, out_w, output_shape[2], output_shape[3],
                                            in_nbytes_c, out_nbytes_c,
                                            in_h, in_w, input_shape[2], input_shape[3],
                                            out_ntiles_hw, out_ntiles_c,
                                            in_cb_pagesize, out_cb_pagesize,
                                            in_cb_page_nelems_padded, out_w_loop_count,
                                            in_log_base_2_of_page_size};
    auto reader_config = DataMovementConfig{.processor = DataMovementProcessor::RISCV_1,
                                            .noc = NOC::RISCV_1_default,
                                            .compile_args = reader_ct_args};
    std::string reader_kernel_fname("tt_metal/kernels/dataflow/reader_max_pool_2d_single_core.cpp");
    auto reader_kernel = CreateDataMovementKernel(program,
                                                  reader_kernel_fname,
                                                  cores,
                                                  reader_config);
    SetRuntimeArgs(program, reader_kernel, cores, reader_rt_args);

    #if 0
    {   // debug
        log_debug("in_cb :: PS = {}, NP = {}", in_cb_pagesize, in_cb_npages);
        log_debug("in_scalar_cb :: PS = {}, NP = {}", in_scalar_cb_pagesize, in_scalar_cb_npages);
        log_debug("in_tiled_cb :: PS = {}, NP = {}", in_tiled_cb_pagesize, in_tiled_cb_npages);
        log_debug("out_tiled_cb :: PS = {}, NP = {}", out_tiled_cb_pagesize, out_tiled_cb_npages);
        log_debug("out_cb :: PS = {}, NP = {}", out_cb_pagesize, out_cb_npages);

        log_debug("in_addr: {}", src_dram_buffer->address());
        log_debug("out_addr: {}", dst_dram_buffer->address());
        log_debug("kernel_size_h: {}", kernel_size_h);
        log_debug("kernel_size_w: {}", kernel_size_w);
        log_debug("kernel_size_hw: {}", kernel_size_hw);
        log_debug("kernel_size_hw_padded: {}", kernel_size_hw_padded);
        log_debug("stride_h: {}", stride_h);
        log_debug("stride_w: {}", stride_w);
        log_debug("pad_h: {}", pad_h);
        log_debug("pad_w: {}", pad_w);
        log_debug("out_h: {}", out_h);
        log_debug("out_w: {}", out_w);
        log_debug("out_hw: {}", output_shape[2]);
        log_debug("out_c: {}", output_shape[3]);
        log_debug("in_nbytes_c: {}", in_nbytes_c);
        log_debug("out_nbytes_c: {}", out_nbytes_c);
        log_debug("in_h: {}", in_h);
        log_debug("in_w: {}", in_w);
        log_debug("in_hw: {}", input_shape[2]);
        log_debug("in_c: {}", input_shape[3]);
        log_debug("out_ntiles_hw: {}", out_ntiles_hw);
        log_debug("out_ntiles_c: {}", out_ntiles_c);
        log_debug("out_nelems: {}", out_nelems);
        log_debug("out_w_loop_count: {}", out_w_loop_count);
    }
    #endif

    /**
     * Writer Kernel: output cb -> output rows
     */
    std::vector<uint32_t> writer_ct_args = reader_ct_args;
    std::vector<uint32_t> writer_rt_args = reader_rt_args;
    auto writer_config = DataMovementConfig{.processor = DataMovementProcessor::RISCV_0,
                                            .noc = NOC::RISCV_0_default,
                                            .compile_args = writer_ct_args};
    std::string writer_kernel_fname("tt_metal/kernels/dataflow/writer_max_pool_2d_single_core.cpp");
    auto writer_kernel = CreateDataMovementKernel(program,
                                                  writer_kernel_fname,
                                                  cores,
                                                  writer_config);
    SetRuntimeArgs(program, writer_kernel, cores, writer_rt_args);

    /**
     * Compute Kernel: input cb -> tilize_block -> input tiles -> reduce_h max -> output tiles -> untilize_block -> output cb
     */
    auto reduce_op = ReduceOpMath::MAX;
    auto reduce_dim = ReduceOpDim::H;
    auto compute_config = ComputeConfig{.math_fidelity = MathFidelity::HiFi4,
                                        .fp32_dest_acc_en = false,
                                        .math_approx_mode = false,
                                        .compile_args = {in_ntiles_hw,
                                                         in_ntiles_c,
                                                         in_ntiles_hw * in_ntiles_c,
                                                         kernel_size_hw_padded,
                                                         out_h,
                                                         out_w,
                                                         (uint32_t) ceil((float) output_shape[2] / constants::TILE_HEIGHT),
                                                         (uint32_t) ceil((float) output_shape[3] / constants::TILE_WIDTH),
                                                         out_nelems, out_w_loop_count},
                                        .defines = reduce_op_utils::get_defines(reduce_op, reduce_dim)};
    std::string compute_kernel_fname("tt_metal/kernels/compute/max_pool.cpp");
    auto compute_kernel = CreateComputeKernel(program,
                                              compute_kernel_fname,
                                              cores,
                                              compute_config);

    auto override_runtime_args_callback =
        [reader_kernel, writer_kernel](const Program& program,
                                       const std::vector<Buffer*>& input_buffers,
                                       const std::vector<Buffer*>& output_buffers) {
        auto src_dram_buffer = input_buffers.at(0);
        auto dst_dram_buffer = output_buffers.at(0);
        CoreCoord core = {0, 0};
        {
            auto runtime_args = GetRuntimeArgs(program, reader_kernel, core);
            runtime_args[0] = src_dram_buffer->address();
            runtime_args[1] = dst_dram_buffer->address();
            SetRuntimeArgs(program, reader_kernel, core, runtime_args);
        }
        {
            auto runtime_args = GetRuntimeArgs(program, writer_kernel, core);
            runtime_args[0] = src_dram_buffer->address();
            runtime_args[1] = dst_dram_buffer->address();
            SetRuntimeArgs(program, writer_kernel, core, runtime_args);
        }
    };
    return {std::move(program), override_runtime_args_callback};
}

void MaxPool::validate(const std::vector<Tensor> &input_tensors) const {
    const auto& input = input_tensors.at(0);
    TT_ASSERT(input.storage_type() == StorageType::DEVICE, "Operands to reshape need to be on device!");
    TT_ASSERT(input.buffer() != nullptr , "Operands to reshape need to be allocated in buffers on device!");
    TT_ASSERT(input.dtype() == DataType::BFLOAT16, "Only BFLOAT16 supported for now");
    TT_ASSERT(input.layout() == Layout::ROW_MAJOR, "Only ROW_MAJOR supported for now");

    // NOTE: This is not a hard requirement. If need to support non-power-of-2, simply change the address generator in reader to generic one.
    uint32_t in_nbytes_c = (input.shape()[3]) * (input.dtype() == DataType::BFLOAT16 ? 2 : 1);
    bool is_pow2 = (in_nbytes_c & (in_nbytes_c - 1)) == 0;
    TT_ASSERT(is_pow2, "Row size (nchannels * bytes = {}) should be power of 2 ({}).", in_nbytes_c, is_pow2);

    TT_ASSERT(2 * pad_h_ < kernel_size_h_ && 2 * pad_w_ < kernel_size_w_,
              "Total padding along a dim should be less than kernel/window size along same dim");
    TT_ASSERT(out_w_ % nblocks_ == 0, "Make sure out_w is divisible by nblocks for now.");
}

std::vector<Shape> MaxPool::compute_output_shapes(const std::vector<Tensor> &input_tensors) const {
    // NOTE: Only for RM
    // NOTE2: Assuming { N, 1, H * W, C }
    // NOTE3: Assuming output data type is same as input
    const auto& input = input_tensors.at(0);
    const auto input_shape = input.shape().without_padding();
    // confirm that the output size supplied to the function matches
    TT_ASSERT(out_h_ == ((in_h_ + 2 * pad_h_ - (dilation_h_ * kernel_size_h_ - 1) - 1) / stride_h_) + 1);
    TT_ASSERT(out_w_ == ((in_w_ + 2 * pad_w_ - (dilation_w_ * kernel_size_w_ - 1) - 1) / stride_w_) + 1);
    uint32_t out_h = out_h_;
    uint32_t out_w = out_w_;
    // need to pad the last dim to TILE_WIDTH
    uint32_t out_c = input_shape[3];
    uint32_t out_c_padded = ceil_multiple_of(out_c, constants::TILE_WIDTH);
    uint32_t out_pagesize = out_c_padded * datum_size(datatype_to_dataformat_converter(input.dtype()));
    uint32_t out_hw = out_h * out_w;
    uint32_t out_hw_padded = (uint32_t) ceil_multiple_of(out_hw, constants::TILE_HEIGHT);

    // {N, 1, H * W, C}
    const auto out_dims = std::vector<uint32_t>({ input_shape[0], 1, out_hw, out_c });
    const auto padding = Padding({{0, 0},
                                  {0, 0},
                                  {0, out_hw_padded - out_hw},
                                  {0, out_c_padded - out_c}},
                                 Padding::PadValue::NegativeInfinity);

    auto out_shape = Shape{out_dims, padding};

    return {out_shape};
}

std::vector<Tensor> MaxPool::create_output_tensors(const std::vector<Tensor> &inputs) const {
    const auto& input = inputs.at(0);
    return operation::generic_create_output_tensors(*this, inputs, input.dtype(), input.layout(), out_mem_config_);
}

operation::ProgramWithCallbacks MaxPool::create_program(const std::vector<Tensor>& inputs, std::vector<Tensor> &outputs) const {
    const auto& input = inputs.at(0);
    auto& output = outputs.at(0);
    return {max_pool_2d_single_core(input, output,
                                    in_h_, in_w_,
                                    out_h_, out_w_,
                                    kernel_size_h_, kernel_size_w_,
                                    stride_h_, stride_w_,
                                    pad_h_, pad_w_,
                                    dilation_h_, dilation_w_,
                                    out_mem_config_,
                                    nblocks_)};
}

tt::stl::reflection::Attributes MaxPool::attributes() const {
    return {
        {"in_h", in_h_},    // input height
        {"in_w", in_w_},    // input width
        {"kernel_size_h", kernel_size_h_},
        {"kernel_size_w", kernel_size_w_},
        {"stride_h", stride_h_},
        {"stride_w", stride_w_},
        {"pad_h", pad_h_},
        {"pad_w", pad_w_},
        {"dilation_h", dilation_h_},
        {"dilation_w", dilation_w_},
    };
}

Tensor max_pool2d(const Tensor &input,
                  uint32_t in_h, uint32_t in_w,
                  uint32_t kernel_size_h, uint32_t kernel_size_w,
                  uint32_t stride_h, uint32_t stride_w,
                  uint32_t pad_h, uint32_t pad_w,
                  uint32_t dilation_h, uint32_t dilation_w,
                  const MemoryConfig& out_mem_config,
                  uint32_t nblocks) {
    TT_ASSERT(dilation_h == 1 && dilation_w == 1 && "Dilation not yet supported in max_pool2d.");
    TT_ASSERT(pad_h < 2 && pad_w < 2 && "Padding > 1 not yet supported.");
    TT_ASSERT(stride_h == stride_w && "Stride should be equal for both H and W for now.");
    // calculate the H and W dims for output
    uint32_t out_h = ((in_h + 2 * pad_h - (dilation_h * kernel_size_h - 1) - 1) / stride_h) + 1;   // floor
    uint32_t out_w = ((in_w + 2 * pad_w - (dilation_w * kernel_size_w - 1) - 1) / stride_w) + 1;   // floor
    return operation::run_without_autoformat(MaxPool{in_h, in_w, out_h, out_w,
                                                     kernel_size_h, kernel_size_w,
                                                     stride_h, stride_w,
                                                     pad_h, pad_w,
                                                     dilation_h, dilation_w,
                                                     out_mem_config,
                                                     nblocks},
                                             {input}).at(0);
}

} // namespace tt_metal
} // namespace tt
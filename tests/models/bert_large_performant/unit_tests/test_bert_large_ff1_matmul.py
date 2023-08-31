from pathlib import Path
import sys
from loguru import logger

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/../../../..")

import numpy as np

import tt_lib as ttl
from models.utility_functions import (
    comp_pcc,
)
import torch
import pytest


def run_bert_large_ff1_matmul_test(
    dtype,
    in0_mem_config,
    in1_mem_config,
    bias_mem_config,
    out_mem_config,
    gelu_activation,
):
    if (
        dtype == ttl.tensor.DataType.BFLOAT16
        and out_mem_config.buffer_type == ttl.tensor.BufferType.L1
        and (
            in0_mem_config.buffer_type == ttl.tensor.BufferType.L1
            or in1_mem_config.buffer_type == ttl.tensor.BufferType.L1
        )
    ):
        pytest.skip("Skipping test since these tensors won't fit on device!")

    torch.manual_seed(1234)
    device = ttl.device.CreateDevice(ttl.device.Arch.GRAYSKULL, 0)
    ttl.device.InitializeDevice(device)
    a_shape = [9, 1, 384, 1024]
    b_shape = [1, 1, 1024, 4096]
    bias_shape = [1, 1, 1, 4096]
    bias_pad_shape = [1, 1, 32, 4096]
    A = torch.randn(a_shape)
    B = torch.randn(b_shape) - 0.95
    BIAS = torch.randint(-20, 20, bias_shape, dtype=torch.float)

    a_t = (
        ttl.tensor.Tensor(
            A.flatten().tolist(),
            a_shape,
            dtype,
            ttl.tensor.Layout.ROW_MAJOR,
        )
        .to(ttl.tensor.Layout.TILE)
        .to(device, in0_mem_config)
    )
    b_t = (
        ttl.tensor.Tensor(
            B.flatten().tolist(),
            b_shape,
            dtype,
            ttl.tensor.Layout.ROW_MAJOR,
        )
        .to(ttl.tensor.Layout.TILE)
        .to(device, in1_mem_config)
    )

    if bias_mem_config is not None:
        bias_t = (
            ttl.tensor.Tensor(
                BIAS.flatten().tolist(),
                bias_shape,
                dtype,
                ttl.tensor.Layout.ROW_MAJOR,
            )
            .pad(bias_pad_shape, [0, 0, 0, 0], 0)
            .to(ttl.tensor.Layout.TILE)
            .to(device, bias_mem_config)
        )
    else:
        bias_t = None

    program_config = ttl.operations.primary.MatmulMultiCoreReuseMultiCastProgramConfig(
        compute_with_storage_grid_size = (12, a_t.shape()[0]),
        in0_block_w = 4,
        out_subblock_h = 6,
        out_subblock_w = 1,
        per_core_M = 12,
        per_core_N = 11,
        fuse_gelu_activation=gelu_activation,
    )
    t2 = ttl.operations.primary.matmul(
        a_t, b_t, bias=bias_t, program_config=program_config, output_mem_config=out_mem_config,
    )
    # Check memory of inputs and outputs
    assert a_t.memory_config().buffer_type == in0_mem_config.buffer_type
    assert b_t.memory_config().buffer_type == in1_mem_config.buffer_type
    if bias_mem_config is not None:
        assert bias_t.memory_config().buffer_type == bias_mem_config.buffer_type
    assert t2.memory_config().buffer_type == out_mem_config.buffer_type
    logger.debug(f"in0 is on: {a_t.memory_config().buffer_type}")
    logger.debug(f"in1 is on: {b_t.memory_config().buffer_type}")
    if bias_mem_config is not None:
        logger.debug(f"bias is on: {bias_t.memory_config().buffer_type}")
    logger.debug(f"out is on: {t2.memory_config().buffer_type}")

    assert t2.shape() == [9, 1, 384, 4096]
    tt_host_rm = t2.cpu().to(ttl.tensor.Layout.ROW_MAJOR)
    pyt_got_back_rm = tt_host_rm.to_torch()

    ref_bmm = torch.matmul(A, B)
    if bias_mem_config is not None:
        ref_bmm = ref_bmm + BIAS
    if gelu_activation:
        ref_bmm = torch.nn.functional.gelu(ref_bmm)
    passing_pcc, output_pcc = comp_pcc(ref_bmm, pyt_got_back_rm, 0.99)
    logger.info(f"Passing={passing_pcc}")
    logger.info(f"Output pcc={output_pcc}")
    ttl.device.CloseDevice(device)
    assert passing_pcc


@pytest.mark.parametrize(
    "gelu_activation",
    (True, False),
    ids=["gelu_activation", "no_activation"],
)
@pytest.mark.parametrize(
    "out_mem_config",
    (
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.L1),
    ),
    ids=["out_DRAM", "out_L1"],
)
@pytest.mark.parametrize(
    "bias_mem_config",
    (
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.L1),
        None,
    ),
    ids=["bias_DRAM", "bias_L1", "bias_None"],
)
@pytest.mark.parametrize(
    "in1_mem_config",
    (
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.L1),
    ),
    ids=["in1_DRAM", "in1_L1"],
)
@pytest.mark.parametrize(
    "in0_mem_config",
    (
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.L1),
    ),
    ids=["in0_DRAM", "in0_L1"],
)
@pytest.mark.parametrize(
    "dtype",
    (ttl.tensor.DataType.BFLOAT8_B, ttl.tensor.DataType.BFLOAT16),
    ids=["BFLOAT8_B", "BFLOAT16"],
)
def test_bert_large_ff1_matmul_test(
    dtype,
    in0_mem_config,
    in1_mem_config,
    bias_mem_config,
    out_mem_config,
    gelu_activation,
    request,
):
    ttl.profiler.set_profiler_location(
        f"tt_metal/tools/profiler/logs/BERT_large_ff1_matmul_{request.node.callspec.id}"
    )
    run_bert_large_ff1_matmul_test(
        dtype,
        in0_mem_config,
        in1_mem_config,
        bias_mem_config,
        out_mem_config,
        gelu_activation,
    )


def test_bert_large_ff1_matmul_with_program_cache(use_program_cache):
    dtype = ttl.tensor.DataType.BFLOAT8_B
    dram_mem_config = ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.DRAM)
    for _ in range(2):
        run_bert_large_ff1_matmul_test(
            dtype,
            dram_mem_config,
            dram_mem_config,
            dram_mem_config,
            dram_mem_config,
            gelu_activation=False,
        )

    dram_mem_config = ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.L1)
    for _ in range(2):
        run_bert_large_ff1_matmul_test(
            dtype,
            dram_mem_config,
            dram_mem_config,
            dram_mem_config,
            dram_mem_config,
            gelu_activation=True,
        )

    assert ttl.program_cache.num_entries() == 2
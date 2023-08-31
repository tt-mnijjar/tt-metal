import sys
import pytest
import math

from pathlib import Path
from loguru import logger

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/../..")

import torch

import tt_lib as ttl

from tt_lib.utils import _nearest_32
from models.utility_functions import comp_pcc


def volume(shape):
    vol = 1.0
    for d in shape:
        vol *= d
    return vol


## max-pool params:
## kernel_h, kernel_w
## stride_h, stride_w
## pad_h, pad_w
## dilation_h, dilation_w
@pytest.mark.parametrize(
    "act_shape",  ## NCHW
    (
        (  # [1, 1, 32, 32],
            [1, 32, 32, 32],
            # [1, 32, 64, 64],
            [1, 64, 64, 64],
            [1, 64, 112, 112],
            [1, 1, 128, 128],
        )
    ),
)
@pytest.mark.parametrize(
    "kernel_size",
    (
        (1, 1),
        (3, 3),
    ),
)
@pytest.mark.parametrize(
    "padding",
    (
        (0, 0),  ## default
        (1, 1),
    ),
)
@pytest.mark.parametrize(
    "stride",
    (
        (1, 1),  ## default
        (2, 2),
    ),
)
@pytest.mark.parametrize("dilation", ((1, 1),))  ## default
@pytest.mark.parametrize(
    "in_mem_config",
    (
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.DRAM),
        ttl.tensor.MemoryConfig(True, ttl.tensor.BufferType.L1),
    ),
    ids=["in_DRAM", "in_L1"],
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
    "nblocks",
    (
        1,  ## default
        8,
    ),
)
def test_run_max_pool(
    act_shape,
    kernel_size,
    padding,
    stride,
    dilation,
    in_mem_config,
    out_mem_config,
    nblocks,
    device,
):
    in_n, in_c, in_h, in_w = act_shape
    kernel_h, kernel_w = kernel_size
    pad_h, pad_w = padding
    stride_h, stride_w = stride
    dilation_h, dilation_w = dilation

    if 2 * pad_h > kernel_h or 2 * pad_w > kernel_w:
        logger.info("Invalid case")
        pytest.skip()

    out_h = (
        math.floor((in_h + 2 * pad_h - (dilation_h * kernel_h - 1) - 1) / stride_h) + 1
    )
    out_w = (
        math.floor((in_w + 2 * pad_w - (dilation_w * kernel_w - 1) - 1) / stride_w) + 1
    )
    if out_w % nblocks != 0:
        logger.info(f"Unsupported case when out_w ({out_w}) % nblocks ({nblocks}) != 0")
        pytest.skip()

    torch.set_printoptions(
        precision=3, sci_mode=False, linewidth=500, threshold=10000, edgeitems=32
    )

    torch.manual_seed(0)

    ## construct the tensor in NCHW shape
    act = torch.randn(act_shape, dtype=torch.bfloat16).float()
    # act = torch.zeros(act_shape, dtype=torch.bfloat16).float()
    # act = torch.ones(act_shape, dtype=torch.bfloat16).float()
    # act = torch.arange(0, volume(act_shape), dtype=torch.bfloat16).reshape(act_shape).float()
    # for c in range(act_shape[1]):
    #     for h in range(act_shape[2]):
    #         for w in range(act_shape[3]):
    #             act[0, c, h, w] = c + h + w

    ## this op expects input tensor as { N, 1, H * W, C }, so rearrange and reshape tensor
    ## but before that, make sure in_c is multiple of tile width
    act_shape = (in_n, 1, in_h * in_w, in_c)
    act_permuted = torch.permute(act, (0, 2, 3, 1))
    act_reshaped = act_permuted.reshape(act_shape)

    act_shape_padded = (in_n, 1, in_h * in_w, _nearest_32(in_c))
    act_padding = (0, act_shape_padded[3] - act_shape[3])
    act_padded = torch.nn.functional.pad(act_reshaped, act_padding, value=0xFF7F)
    assert act_shape_padded == act_padded.shape

    ttact = ttl.tensor.Tensor(
        act_padded.flatten().tolist(),
        act_shape_padded,
        ttl.tensor.DataType.BFLOAT16,
        ttl.tensor.Layout.ROW_MAJOR,
    )
    ttact = ttact.to(device, in_mem_config)

    out_padded = ttl.tensor.max_pool2d(
        ttact,
        in_h,
        in_w,
        kernel_h,
        kernel_w,
        stride_h,
        stride_w,
        pad_h,
        pad_w,
        dilation_h,
        dilation_w,
        out_mem_config,
        nblocks,
    )
    out_padded = out_padded.cpu().to(ttl.tensor.Layout.ROW_MAJOR)

    out_shape_padded = out_padded.shape()
    out_pytorch_padded = out_padded.to_torch().reshape(out_shape_padded)  ## N, 1, HW, C
    out_pytorch = out_pytorch_padded[:, :, :, :in_c]
    out_pytorch = torch.permute(out_pytorch, (0, 3, 1, 2))  ## N, C, 1, HW

    ## reference
    golden_pytorch = torch.nn.MaxPool2d(
        kernel_size,
        stride=stride,
        padding=padding,
        dilation=1,
        return_indices=False,
        ceil_mode=False,
    )(act)

    ## test for equivalance
    out_pytorch = out_pytorch.reshape(golden_pytorch.shape)
    passing_pcc, output_pcc = comp_pcc(golden_pytorch, out_pytorch)
    logger.info(f"Passing PCC = {passing_pcc}")
    logger.info(f"Output PCC = {output_pcc}")
    # print(f'OUTPUT: {out_pytorch}')
    # print(f'GOLDEN: {golden_pytorch}')

    assert passing_pcc
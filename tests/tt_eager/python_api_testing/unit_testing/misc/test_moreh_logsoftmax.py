# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch

import tt_lib as ttl
import pytest
from models.utility_functions import comp_allclose_and_pcc
from loguru import logger
import torch.nn.functional as F


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((1, 1, 32, 32), 3),  # single tile
        ((1, 1, 32, 32 * 5), 3),  # mutiple tile with dim W
        ((5, 6, 32, 32), 3),  # multiple cores
        ((10, 20, 32 * 3, 32 * 5), 3),  # multiple tiles per core
        ((1, 1, 32, 32), 2),  # single tile
        ((1, 1, 32 * 5, 32), 2),  # mutiple tile with dim H
        ((5, 6, 32, 32), 2),  # multiple cores
        ((10, 20, 32 * 3, 32 * 5), 2),  # multiple tiles per core
    ),
)
def test_logsoftmax_for_dim_hw(shape_dim, device):
    ttl.program_cache.enable()

    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)

    dev_x = ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)
    dev_y = ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    tt_cpu = F.log_softmax(x, dim)
    tt_npu = ttl.operations.primary.moreh_logsoftmax(dev_x, dev_y, dim)

    assert list(tt_npu.shape()) == list(tt_cpu.shape)
    tt_dev = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).to_torch().to(torch.bfloat16)

    rtol = atol = 0.1
    passing, out = comp_allclose_and_pcc(tt_cpu, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((2, 3, 32 * 4, 32 * 5), 3),
        ((2, 3, 32 * 4, 32 * 5), 2),
    ),
)
def test_logsoftmax_large_algorithm_for_dim_hw(shape_dim, device):
    ttl.program_cache.enable()

    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)

    dev_x = ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)
    dev_y = ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    tt_cpu = F.log_softmax(x, dim)
    strategy = (
        ttl.operations.primary.MorehSoftmaxOpParallelizationStrategy.LARGE_W
        if dim == 3
        else ttl.operations.primary.MorehSoftmaxOpParallelizationStrategy.LARGE_H
    )
    tt_npu = ttl.operations.primary.moreh_logsoftmax(dev_x, dev_y, dim, strategy)

    assert list(tt_npu.shape()) == list(tt_cpu.shape)
    tt_dev = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).to_torch().to(torch.bfloat16)

    rtol = atol = 0.1
    passing, out = comp_allclose_and_pcc(tt_cpu, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((1, 1, 10, 15), 3),  # single tile
        ((1, 1, 10, 32 * 2 + 10), 3),  # mutiple tile with dim
        ((1, 1, 15, 10), 2),  # single tile
        ((1, 1, 32 * 2 + 10, 32), 2),  # mutiple tile with dim
    ),
)
def test_logsoftmax_not_multiple_of_32_for_dim_hw(shape_dim, device):
    ttl.program_cache.enable()
    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)

    dev_x = (
        ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("nan"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )
    dev_y = (
        ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("nan"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )

    tt_cpu = F.log_softmax(x, dim)
    tt_npu = ttl.operations.primary.moreh_logsoftmax(dev_x, dev_y, dim)
    tt_npu = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).unpad_from_tile((N, C, H, W))

    assert list(tt_npu.shape()) == list(tt_cpu.shape)
    tt_dev = tt_npu.to_torch().to(torch.bfloat16)

    rtol = atol = 0.1
    passing, out = comp_allclose_and_pcc(tt_cpu, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((1, 15, 32, 32), 1),  # single tile c
        ((1, 15, 32 * 7, 32 * 5), 1),  # mutiple cores
        ((109, 15, 32, 32), 1),  # mutiple tiles per cores
        ((15, 1, 32, 32), 0),  # single tile n
        ((15, 1, 32 * 7, 32 * 5), 0),  # mutiple cores
        ((15, 109, 32 * 2, 32 * 2), 0),  # mutiple tiles per cores
    ),
)
def test_logsoftmax_for_dim_nc(shape_dim, device):
    ttl.program_cache.enable()
    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)

    dev_x = (
        ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16).pad_to_tile(float("7")).to(ttl.tensor.Layout.TILE).to(device)
    )
    dev_y = (
        ttl.tensor.Tensor(x, ttl.tensor.DataType.BFLOAT16).pad_to_tile(float("7")).to(ttl.tensor.Layout.TILE).to(device)
    )

    tt_cpu = F.log_softmax(x, dim)
    tt_npu = ttl.operations.primary.moreh_logsoftmax(dev_x, dev_y, dim)
    tt_npu = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).unpad_from_tile((N, C, H, W))

    assert list(tt_npu.shape()) == list(tt_cpu.shape)
    tt_dev = tt_npu.to_torch().to(torch.bfloat16)

    rtol = atol = 0.1
    passing, out = comp_allclose_and_pcc(tt_cpu, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((1, 1, 32, 32), 3),  # single tile
        ((1, 1, 32, 32 * 2), 3),  # mutiple tile with dim W
        ((5, 6, 32, 32), 3),  # multiple cores
        ((10, 20, 32 * 3, 32 * 5), 3),  # multiple tiles per core
        ((1, 1, 32, 32), 2),  # single tile
        ((1, 1, 32 * 5, 32), 2),  # mutiple tile with dim H
        ((5, 6, 32, 32), 2),  # multiple cores
        ((10, 20, 32 * 5, 32), 2),  # multiple tiles per core
    ),
)
def test_logsoftmax_backward_for_dim_hw(shape_dim, device):
    ttl.program_cache.enable()
    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = (
        torch.randint(low=0, high=4, size=(N * C * H * W,))
        .reshape((N, C, H, W))
        .to(torch.bfloat16)
        .requires_grad_(True)
    )

    y = F.log_softmax(x, dim)
    dev_y = ttl.tensor.Tensor(y, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    dy = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)
    dev_dy = ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    dev_dx = ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    y.backward(dy)
    tt_npu = ttl.operations.primary.moreh_logsoftmax_backward(dev_y, dev_dy, dev_dx, dim)

    assert list(tt_npu.shape()) == list(x.grad.shape)
    tt_dev = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).to_torch().to(torch.bfloat16)

    rtol = atol = 0.5
    passing, out = comp_allclose_and_pcc(x.grad, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((2, 3, 32 * 4, 32 * 5), 3),
        ((2, 3, 32 * 4, 32 * 5), 2),
    ),
)
def test_logsoftmax_backward_large_algorithm_for_dim_hw(shape_dim, device):
    ttl.program_cache.enable()
    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = (
        torch.randint(low=0, high=4, size=(N * C * H * W,))
        .reshape((N, C, H, W))
        .to(torch.bfloat16)
        .requires_grad_(True)
    )

    y = F.log_softmax(x, dim)
    dev_y = ttl.tensor.Tensor(y, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    dy = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)
    dev_dy = ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    dev_dx = ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16).to(ttl.tensor.Layout.TILE).to(device)

    y.backward(dy)
    strategy = (
        ttl.operations.primary.MorehSoftmaxBackwardOpParallelizationStrategy.LARGE_W
        if dim == 3
        else ttl.operations.primary.MorehSoftmaxBackwardOpParallelizationStrategy.LARGE_H
    )
    tt_npu = ttl.operations.primary.moreh_logsoftmax_backward(dev_y, dev_dy, dev_dx, dim, strategy)

    assert list(tt_npu.shape()) == list(x.grad.shape)
    tt_dev = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).to_torch().to(torch.bfloat16)

    rtol = atol = 0.5
    passing, out = comp_allclose_and_pcc(x.grad, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((1, 1, 10, 15), 3),  # single tile
        ((1, 1, 10, 32 * 2 + 10), 3),  # mutiple tile with dim
        ((1, 1, 15, 10), 2),  # single tile
        ((1, 1, 32 * 2 + 10, 32), 2),  # mutiple tile with dim
    ),
)
def test_logsoftmax_backward_not_multiple_of_32_for_dim_hw(shape_dim, device):
    ttl.program_cache.enable()
    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = (
        torch.randint(low=0, high=4, size=(N * C * H * W,))
        .reshape((N, C, H, W))
        .to(torch.bfloat16)
        .requires_grad_(True)
    )

    y = F.log_softmax(x, dim)
    dev_y = (
        ttl.tensor.Tensor(y, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("10"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )

    dy = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)
    dev_dy = (
        ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("200"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )

    dev_dx = (
        ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("200"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )

    y.backward(dy)
    tt_npu = ttl.operations.primary.moreh_logsoftmax_backward(dev_y, dev_dy, dev_dx, dim)
    tt_npu = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).unpad_from_tile((N, C, H, W))

    assert list(tt_npu.shape()) == list(x.grad.shape)
    tt_dev = tt_npu.to_torch().to(torch.bfloat16)

    rtol = atol = 0.1
    passing, out = comp_allclose_and_pcc(x.grad, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing


@pytest.mark.parametrize(
    "shape_dim",
    (
        ((1, 15, 32, 32), 1),  # single tile c
        ((1, 15, 32 * 7, 32 * 5), 1),  # mutiple cores
        ((109, 15, 32, 32), 1),  # mutiple tiles per cores
        ((15, 1, 32, 32), 0),  # single tile n
        ((15, 1, 32 * 7, 32 * 5), 0),  # mutiple cores
        ((15, 109, 32 * 2, 32 * 2), 0),  # mutiple tiles per cores
    ),
)
def test_logsoftmax_backward_for_dim_nc(shape_dim, device):
    ttl.program_cache.enable()
    shape, dim = shape_dim
    torch.manual_seed(0)

    N = shape[0]
    C = shape[1]
    H = shape[2]
    W = shape[3]

    x = (
        torch.randint(low=0, high=4, size=(N * C * H * W,))
        .reshape((N, C, H, W))
        .to(torch.bfloat16)
        .requires_grad_(True)
    )

    y = F.log_softmax(x, dim)
    dev_y = (
        ttl.tensor.Tensor(y, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("10"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )

    dy = torch.randint(low=0, high=4, size=(N * C * H * W,)).reshape((N, C, H, W)).to(torch.bfloat16)
    dev_dy = (
        ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("10"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )

    dev_dx = (
        ttl.tensor.Tensor(dy, ttl.tensor.DataType.BFLOAT16)
        .pad_to_tile(float("10"))
        .to(ttl.tensor.Layout.TILE)
        .to(device)
    )

    y.backward(dy)
    tt_npu = ttl.operations.primary.moreh_logsoftmax_backward(dev_y, dev_dy, dev_dx, dim)
    tt_npu = tt_npu.cpu().to(ttl.tensor.Layout.ROW_MAJOR).unpad_from_tile((N, C, H, W))
    assert list(tt_npu.shape()) == list(x.grad.shape)
    tt_dev = tt_npu.cpu().to_torch().to(torch.bfloat16)

    rtol = atol = 0.5
    passing, out = comp_allclose_and_pcc(x.grad, tt_dev, rtol=rtol, atol=atol)
    logger.info(out)
    assert passing

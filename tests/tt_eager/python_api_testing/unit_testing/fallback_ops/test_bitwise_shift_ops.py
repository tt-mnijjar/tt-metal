# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch
import tt_lib as ttl
import tt_lib.fallback_ops
from tests.tt_eager.python_api_testing.sweep_tests import (
    comparison_funcs,
)
from loguru import logger
import pytest


@pytest.mark.parametrize("on_device", [True, False])
@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
@pytest.mark.parametrize("shift_kind", ["right", "left"])
class TestBitwiseShiftOps:
    @pytest.mark.parametrize("other", [5, 10])
    def test_bitwise_unary_shift_fallback(self, input_shapes, other, shift_kind, on_device, device):
        torch.manual_seed(1234)

        x = torch.randint(low=0, high=100, size=input_shapes)
        # Test on host RM
        t0 = ttl.tensor.Tensor(
            x,
            ttl.tensor.DataType.UINT32,
        )
        if on_device:
            t0 = t0.to(device)
        if shift_kind == "right":
            t1 = ttl.fallback_ops.unary_bitwise_right_shift(t0, other)
            pt_out = torch.bitwise_right_shift(x, other)
        elif shift_kind == "left":
            t1 = ttl.fallback_ops.unary_bitwise_left_shift(t0, other)
            pt_out = torch.bitwise_left_shift(x, other)

        output = t1.cpu().to(ttl.tensor.Layout.ROW_MAJOR).to_torch()
        comp_pass, _ = comparison_funcs.comp_equal(pt_out, output)
        _, comp_out = comparison_funcs.comp_allclose_and_pcc(pt_out, output)
        logger.info(comp_out)
        assert comp_pass

    def test_bitwise_binary_shift_fallback(self, input_shapes, shift_kind, on_device, device):
        torch.manual_seed(1234)

        x = torch.randint(low=10, high=100, size=input_shapes)
        y = torch.randint(low=1, high=10, size=input_shapes)

        # Test on host RM
        t0 = ttl.tensor.Tensor(
            x,
            ttl.tensor.DataType.UINT32,
        )
        if on_device:
            t0 = t0.to(device)

        t1 = ttl.tensor.Tensor(
            y,
            ttl.tensor.DataType.UINT32,
        )
        if on_device:
            t1 = t1.to(device)

        if shift_kind == "right":
            tout = ttl.fallback_ops.binary_bitwise_right_shift(t0, t1)
            pt_out = torch.bitwise_right_shift(x, y)
        elif shift_kind == "left":
            tout = ttl.fallback_ops.binary_bitwise_left_shift(t0, t1)
            pt_out = torch.bitwise_left_shift(x, y)

        output = tout.cpu().to(ttl.tensor.Layout.ROW_MAJOR).to_torch()
        comp_pass, _ = comparison_funcs.comp_equal(pt_out, output)
        _, comp_out = comparison_funcs.comp_allclose_and_pcc(pt_out, output)
        logger.info(comp_out)
        assert comp_pass

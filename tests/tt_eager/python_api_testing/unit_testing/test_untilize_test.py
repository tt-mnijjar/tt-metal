import pytest
from pathlib import Path
import sys
import torch

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/../..")

import numpy as np

import tt_lib as ttl
from models.utility_functions import untilize


@pytest.mark.parametrize(
    "nb, nc, nh, nw",
    (
        (5, 2, 4, 8),  # fast power of 2 width path
        (5, 2, 4, 7),  # slow non-power of 2 width path
    ),
)
def test_run_untilize_test(nb, nc, nh, nw, device):
    nt = nb * nc * nh * nw
    shape = [nb, nc, 32 * nh, 32 * nw]

    inp = np.random.rand(*shape)

    a = ttl.tensor.Tensor(
        inp.flatten().tolist(),
        shape,
        ttl.tensor.DataType.BFLOAT16,
        ttl.tensor.Layout.TILE,
        device,
    )
    b = ttl.tensor.untilize(a)
    c = b.cpu().to_torch().to(torch.float32).reshape(shape).numpy()

    untilized_inp = untilize(inp.reshape(*shape))
    assert (
        abs(untilized_inp - c) < 0.02
    ).all(), "Max abs difference for untilize can be 0.02 due to bfloat conversions"
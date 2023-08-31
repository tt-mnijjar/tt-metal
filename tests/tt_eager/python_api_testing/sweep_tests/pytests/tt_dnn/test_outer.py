import pytest
import sys
import torch
import tt_lib as ttl
from pathlib import Path
from functools import partial

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/..")
sys.path.append(f"{f}/../..")
sys.path.append(f"{f}/../../..")
sys.path.append(f"{f}/../../../..")


from tests.tt_eager.python_api_testing.sweep_tests import comparison_funcs, generation_funcs
from tests.tt_eager.python_api_testing.sweep_tests.run_pytorch_ci_tests import run_single_pytorch_test
from tests.tt_eager.python_api_testing.sweep_tests.common import skip_for_wormhole_b0, is_wormhole_b0

shapes = [
        # Single core (won't be hit after padding is added for multicast)
        [[1, 1, 32, 1], [1, 1, 1, 32]],  # no reshape
        [[1, 1, 2048, 1], [1, 1, 1, 64]],  # LLaMa dimensions (no reshape)
        # FIXME: padding required for reshape on device
        # [[1, 1, 1, 32], [1, 1, 1, 32]],    #invoke reshape on lhs
        # [[1, 1, 1, 32], [1, 1, 32, 1]],    #invoke reshape on both
        # [[1, 1, 32, 1], [1, 1, 32, 1]],    #invoke reshape on rhs
]
if is_wormhole_b0():
    del shapes[1:]

@pytest.mark.parametrize(
    "input_shapes",
    shapes
)
@pytest.mark.parametrize("pcie_slot", (0,))
@pytest.mark.parametrize("dtype", (ttl.tensor.DataType.BFLOAT16,))
def test_run_outer_test(input_shapes, pcie_slot, dtype, function_level_defaults):
    datagen_func = [
        generation_funcs.gen_func_with_cast(
            partial(generation_funcs.gen_rand, low=-100, high=100), torch.bfloat16
        )
    ] * 2
    comparison_func = partial(comparison_funcs.comp_pcc)
    run_single_pytorch_test(
        "outer",
        input_shapes,
        datagen_func,
        comparison_func,
        pcie_slot,
        {"dtype": dtype, "layout": ttl.tensor.Layout.ROW_MAJOR, "on_device": False},
    )
import pytest
import sys
import torch
from pathlib import Path
from functools import partial

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/..")
sys.path.append(f"{f}/../..")
sys.path.append(f"{f}/../../..")
sys.path.append(f"{f}/../../../..")


from tests.tt_eager.python_api_testing.sweep_tests import comparison_funcs, generation_funcs
from tests.tt_eager.python_api_testing.sweep_tests.run_pytorch_ci_tests import run_single_pytorch_test
import tt_lib as ttl


@pytest.mark.parametrize(
    "input_shapes",
    (([[1, 1, 32, 32]], [[3, 1, 320, 384]], [[1, 1, 128, 7328]])),
)
@pytest.mark.parametrize(
    "untilize_args",
    (
        {
            "dtype": ttl.tensor.DataType.BFLOAT16,
            "on_device": True,
            "layout": ttl.tensor.Layout.TILE,
        },
    ),
)
@pytest.mark.parametrize("pcie_slot", ((0,)))
def test_untilize_test(input_shapes, untilize_args, pcie_slot, function_level_defaults):
    datagen_func = [
        generation_funcs.gen_func_with_cast(
            partial(generation_funcs.gen_arange), torch.bfloat16, True
        )
    ]
    comparison_func = comparison_funcs.comp_equal
    run_single_pytorch_test(
        "untilize",
        input_shapes,
        datagen_func,
        comparison_func,
        pcie_slot,
        untilize_args,
    )
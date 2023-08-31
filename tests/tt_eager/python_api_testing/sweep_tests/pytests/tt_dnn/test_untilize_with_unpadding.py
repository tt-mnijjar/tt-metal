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

params = [
    pytest.param([[5, 5, 32, 32]], untilize_with_unpadding_args, 0)
    for untilize_with_unpadding_args in generation_funcs.gen_untilize_with_unpadding_args(
        [[5, 5, 32, 32]]
    )
]
params += [
    pytest.param([[5, 5, 64, 96]], untilize_with_unpadding_args, 0)
    for untilize_with_unpadding_args in generation_funcs.gen_untilize_with_unpadding_args(
        [[5, 5, 64, 96]]
    )
]

params += [
    pytest.param(
        [[1, 1, 128, 7328]],
        {
            "dtype": ttl.tensor.DataType.BFLOAT16,
            "on_device": True,
            "layout": ttl.tensor.Layout.TILE,
            "output_tensor_start": [0, 0, 0, 0],
            "output_tensor_end": [0, 0, 119, 7299],
        },
        0,
    )
]


@pytest.mark.parametrize(
    "input_shapes, untilize_with_unpadding_args, pcie_slot", params
)
def test_run_untilize_with_unpadding_test(
    input_shapes, untilize_with_unpadding_args, pcie_slot
):
    datagen_func = [
        generation_funcs.gen_func_with_cast(
            partial(generation_funcs.gen_rand, low=-100, high=100), torch.bfloat16, True
        )
    ]
    comparison_func = comparison_funcs.comp_equal
    run_single_pytorch_test(
        "untilize_with_unpadding",
        input_shapes,
        datagen_func,
        comparison_func,
        pcie_slot,
        untilize_with_unpadding_args,
    )
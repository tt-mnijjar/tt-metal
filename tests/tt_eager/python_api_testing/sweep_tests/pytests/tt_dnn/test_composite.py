import pytest
import sys
import torch
from pathlib import Path
from functools import partial
from itertools import product
from collections import defaultdict
from math import pi
import numpy as np

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/..")
sys.path.append(f"{f}/../..")
sys.path.append(f"{f}/../../..")
sys.path.append(f"{f}/../../../..")


from tests.tt_eager.python_api_testing.sweep_tests import comparison_funcs, generation_funcs
from tests.tt_eager.python_api_testing.sweep_tests.run_pytorch_ci_tests import run_single_pytorch_test
from tests.tt_eager.python_api_testing.sweep_tests.common import is_wormhole_b0


reference_pcc = defaultdict(lambda: 0.999)
reference_pcc["silu"] = 0.9714
reference_pcc["swish"] = reference_pcc["silu"]
reference_pcc["softplus"] = 0.9984


def custom_compare(*args, **kwargs):
    function = kwargs.pop("function")
    comparison_func = partial(comparison_funcs.comp_pcc, pcc=reference_pcc[function])
    result = comparison_func(*args, **kwargs)
    return result

shapes =([[1, 1, 32, 32]],[[1, 3, 320, 64]])
if is_wormhole_b0():
    shapes = (shapes[0],)

# TODO: This function should be split apart instead of having all these if cases
@pytest.mark.parametrize(
    "fn, input_shapes, pcie_slot",
    list(
        product(
            (
                "lerp_binary",
                "lerp_ternary",
                "addcmul",
                "addcdiv",
                "min",
                "max",
                "swish",
                "log1p",
                "softplus",
                "mish",
                "silu",
                "polyval",
                "mac",
                "cbrt",
                "threshold",
                "hypot",
                "hardswish",
                "hardsigmoid",
                "ones_like",
                "zeros_like",
                "full_like",
                "ones",
                "zeros",
                "full",
                "arange",
                "hardshrink",
                "softshrink",
                "sinh",
                "cosh",
                "tanhshrink",
                "xlogy",
                "asinh",
                "acosh",
                "atanh",
                "atan2",
                "ldexp",
                "subalpha",
                "logaddexp",
                "logaddexp2",
                "bias_gelu",
            ),
            shapes,
            (0,),
        )
    ),  # Single core, and multi-core
)
def test_run_eltwise_composite_test(
    fn, input_shapes, pcie_slot, function_level_defaults
):
    options = defaultdict(lambda: (-1.0, 1.0))
    options["log1"] = (0.0, 1.0)
    options["polyval"] = (1, 100)

    options["deg2rad"] = (-180, 180)
    options["bias_gelu"] = (-1e-10, 1e10)
    options["rad2deg"] = (0, 2 * pi)
    options["hypot"] = (1, 100)
    options["atan2"] = (-100, 100)
    options["logaddexp"] = (-90, 90)
    options["logaddexp2"] = (-100, 100)
    options["cbrt"] = (-1000, 1000)
    options["hardsigmoid"] = (-100, 100)
    options["hardswish"] = (-100, 100)
    options["hardshrink"] = (-100, 100)
    options["softshrink"] = (-100, 100)
    options["leaky_shrink"] = (-100, 100)
    options["softsign"] = (1, 100)

    options["sinh"] = (-9, 9)
    options["ldexp"] = (-64, 64)
    options["tanhshrink"] = (-100, 100)
    options["atanh"] = (-100, 100)
    options["cosh"] = options["sinh"]
    options["asinh"] = (-100, 100)
    options["acosh"] = (1, 100)

    generator = generation_funcs.gen_rand

    if is_wormhole_b0():
        if fn in ["atanh","ldexp","logaddexp2"]:
            pytest.skip('Not tested for Wormhole - skipping')

    datagen_func = [
        generation_funcs.gen_func_with_cast(
            partial(generator, low=options[fn][0], high=options[fn][1]),
            torch.bfloat16,
        )
    ]
    num_inputs = 1
    if fn in ["mac", "addcmul", "addcdiv", "lerp_ternary"]:
        num_inputs = 3
    elif fn in ["hypot", "min", "max", "lerp_binary", "xlogy", "atan2", "lerp_binary", "atan2", "ldexp", "subalpha", "logaddexp", "logaddexp2"]:
        num_inputs = 2

    input_shapes = input_shapes * num_inputs
    datagen_func = datagen_func * num_inputs
    test_args = generation_funcs.gen_default_dtype_layout_device(input_shapes)[0]
    test_args.update({"scalar": np.random.randint(-100, 100)})
    if fn == "arange":
        test_args.update({"start": -10, "end": 1024 - 10, "step": 1})
    elif fn == "polyval":
        test_args.update({"coeffs": [1.0, 2.0, 1.0, 2.0]})
    elif fn == "threshold":
        test_args.update({"threshold": 5.0, "value": 1.0})
    elif fn in ["softshrink", "hardshrink"]:
        test_args.update({"_lambda": np.random.randint(1, 100)})
    elif fn in ["addcmul", "addcdiv"]:
        test_args.update({"value": np.random.randint(1, 100)})
    elif fn in ["lerp_binary"]:
        test_args.update({"weight": np.random.randint(1, 100)})
    elif fn in ["subalpha"]:
        test_args.update({"alpha": np.random.randint(1, 100)})
    elif fn in ["bias_gelu"]:
        test_args.update({"bias": np.random.randint(1, 100)})
    run_single_pytorch_test(
        "eltwise-%s" % (fn),
        input_shapes,
        datagen_func,
        partial(custom_compare, function=fn),
        pcie_slot,
        test_args,
    )
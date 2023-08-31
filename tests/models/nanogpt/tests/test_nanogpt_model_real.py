from pathlib import Path
import sys

f = f"{Path(__file__).parent}"
sys.path.append(f"{f}/..")
sys.path.append(f"{f}/../..")
sys.path.append(f"{f}/../../..")
sys.path.append(f"{f}/../../../..")

import torch
import tt_lib
import pytest

import os
import pickle
import tiktoken

from transformers import GPT2LMHeadModel

from tests.tt_eager.python_api_testing.sweep_tests.comparison_funcs import comp_allclose, comp_pcc

from loguru import logger
import tests.models.nanogpt.tt.nanogpt_block as nanogpt_block
import tests.models.nanogpt.tt.nanogpt_attention as nanogpt_attention
import tests.models.nanogpt.tt.nanogpt_model as nanogpt_model
from tests.models.nanogpt.tt.nanogpt_config import GPTConfig

from models.utility_functions import (
    torch2tt_tensor,
    tt2torch_tensor,
    torch_to_tt_tensor_rm,
)

@pytest.mark.parametrize(
    "pcc, prompt",
    (
        (
            0.99,
            "How are you?"
        ),
    ),
)
def test_nanogpt_model_real(pcc, prompt):
    device = tt_lib.device.CreateDevice(tt_lib.device.Arch.GRAYSKULL, 0)
    tt_lib.device.InitializeDevice(device)
    tt_lib.device.SetDefaultDevice(device)


    # Prepare input

    model_hf = GPT2LMHeadModel.from_pretrained('gpt2')
    sd = model_hf.state_dict()
    model_hf.eval()
    torch.manual_seed(0)

    block_size = 1024

    enc = tiktoken.get_encoding("gpt2")
    encode = lambda s: enc.encode(s, allowed_special={"<|endoftext|>"})

    start_ids = encode(prompt)

    x = (torch.tensor(start_ids, dtype=torch.long, device='cpu')[None, ...])

    x = x if x.size(1) <= block_size else x[:, -block_size:]


    pt_model = model_hf
    pt_out = pt_model.forward(x)

    model_type = 'gpt2'

    config_args = {
        'gpt2':         dict(n_layer=12, n_head=12, n_embd=768),  # 124M params
    }[model_type]

    config_args['vocab_size'] = 50257 # always 50257 for GPT model checkpoints
    config_args['block_size'] = 1024 # always 1024 for GPT model checkpoints
    config_args['bias'] = True # always True for GPT model checkpoints

    config = GPTConfig(**config_args)

    tt_test_in = torch2tt_tensor(x, device, tt_layout=tt_lib.tensor.Layout.ROW_MAJOR)

    tt_model = nanogpt_model.TtGPT(config, sd, device)
    #tt_model = nanogpt_model.nanogpt(device)

    tt_out = tt_model.forward(
        x
    )

    tt_out_converted = tt2torch_tensor(tt_out[0])

    does_pass, pcc_message = comp_pcc(pt_out[0], tt_out_converted, 0.99)
    logger.info(pcc_message)

    tt_lib.device.CloseDevice(device)

    if does_pass:
        logger.info("nanogpt_model_real: Passed!")
    else:
        logger.warning("nanogpt_model_real: Failed!")

    assert does_pass
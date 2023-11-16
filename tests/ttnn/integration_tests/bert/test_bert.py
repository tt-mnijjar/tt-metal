# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import time

import pytest

from loguru import logger
import torch
import torch.nn.functional as F
import transformers


import ttnn

from models.experimental.functional_bert.ttnn_functional_bert import (
    ttnn_bert_for_question_answering,
)

from models.experimental.functional_bert.ttnn_optimized_functional_bert import (
    ttnn_optimized_bert_for_question_answering,
)
from models.experimental.functional_bert.torch_functional_bert import (
    torch_bert_for_question_answering,
)
from models.experimental.functional_bert.parameters import (
    ParametersConfig,
    preprocess_model_parameters,
    preprocess_linear_bias,
    preprocess_linear_weight,
)
from models.utility_functions import (
    enable_persistent_kernel_cache,
    disable_persistent_kernel_cache,
)

from tests.ttnn.utils_for_testing import assert_with_pcc
from models.utility_functions import skip_for_wormhole_b0


def ttnn_bert_preprocess_inputs(
    input_ids,
    token_type_ids,
    attention_mask,
    **kwargs,
):
    input_ids = ttnn.from_torch(input_ids, dtype=ttnn.uint32)
    input_ids = ttnn.to_device(input_ids, kwargs["device"], memory_config=ttnn.L1_MEMORY_CONFIG)

    token_type_ids = ttnn.from_torch(token_type_ids, dtype=ttnn.uint32)
    token_type_ids = ttnn.to_device(token_type_ids, kwargs["device"], memory_config=ttnn.L1_MEMORY_CONFIG)

    if attention_mask is not None:
        attention_mask = attention_mask.unsqueeze(0).unsqueeze(0)
        attention_mask = F.pad(attention_mask, (0, 0, 0, 31, 0, 0, 0, 7))
        attention_mask = ttnn.from_torch(attention_mask, dtype=ttnn.bfloat16)
        attention_mask = ttnn.to_layout(attention_mask, ttnn.TILE_LAYOUT)
        attention_mask = ttnn.to_device(attention_mask, kwargs["device"], memory_config=ttnn.L1_MEMORY_CONFIG)

    return input_ids, token_type_ids, attention_mask


def is_to_be_converted(torch_model, full_name):
    return True


def custom_preprocessor(parameters_config, torch_model, full_name, **kwargs):
    parameters = {}
    if isinstance(torch_model, transformers.models.bert.modeling_bert.BertSelfAttention):
        qkv_weight = torch.cat(
            [
                torch_model.query.weight,
                torch_model.key.weight,
                torch_model.value.weight,
            ],
            dim=0,
        )
        qkv_bias = torch.cat(
            [torch_model.query.bias, torch_model.key.bias, torch_model.value.bias],
            dim=0,
        )
        parameters[f"{full_name}fused_qkv.weight"] = preprocess_linear_weight(parameters_config, qkv_weight, **kwargs)
        parameters[f"{full_name}fused_qkv.bias"] = preprocess_linear_bias(parameters_config, qkv_bias, **kwargs)
    return parameters


def run_bert_question_and_answering_inference(model_name, batch_size, sequence_size, use_optimized_version, device):
    torch.manual_seed(1234)

    torch_model = transformers.BertForQuestionAnswering.from_pretrained(model_name, torchscript=False).eval()
    config = torch_model.config

    num_encoders = config.num_hidden_layers
    if not use_optimized_version:
        num_encoders = 1
    head_size = config.hidden_size // config.num_attention_heads

    torch_bert_input = torch.randint(0, torch_model.config.vocab_size - 1, (batch_size, sequence_size))
    torch_token_type_ids = torch.zeros((batch_size, sequence_size), dtype=torch.int32)
    torch_attention_mask = torch.zeros(1, sequence_size) if use_optimized_version else None

    torch_output = torch_bert_for_question_answering(
        torch_bert_input,
        torch_token_type_ids,
        torch_attention_mask,
        parameters=torch_model.state_dict(),
        num_encoders=num_encoders,
        head_size=head_size,
    )

    # Run TT Model
    parameters_config = ParametersConfig(
        linear_weight_dtype=ttnn.bfloat16,
        linear_bias_dtype=ttnn.bfloat16,
        layernorm_parameter_dtype=ttnn.bfloat16,
    )
    parameters = preprocess_model_parameters(
        f"{model_name}-{use_optimized_version}",
        parameters_config,
        torch_model,
        is_to_be_converted=is_to_be_converted,
        custom_preprocessor=custom_preprocessor if use_optimized_version else None,
        device=device,
    )

    ttnn_bert_inputs = ttnn_bert_preprocess_inputs(
        torch_bert_input,
        torch_token_type_ids,
        torch_attention_mask,
        device=device,
    )

    bert_for_question_answering = (
        ttnn_optimized_bert_for_question_answering if use_optimized_version else ttnn_bert_for_question_answering
    )

    start = time.time()
    tt_output = bert_for_question_answering(
        *ttnn_bert_inputs,
        parameters=parameters,
        num_encoders=num_encoders,
        head_size=head_size,
    )
    end = time.time()

    duration = end - start
    logger.info(f"Duration: {duration}")
    logger.info(f"Samples per second: {1 / duration * batch_size}")

    tt_output = ttnn.to_layout(tt_output, ttnn.ROW_MAJOR_LAYOUT)
    tt_output = ttnn.from_device(tt_output)
    tt_output = ttnn.to_torch(tt_output)[..., :2]

    # if not use_optimized_version:
    #     assert_with_pcc(torch_output, tt_output, 0.9547)


@skip_for_wormhole_b0()
@pytest.mark.parametrize("model_name", ["phiyodr/bert-large-finetuned-squad2"])
@pytest.mark.parametrize("batch_size", [8])
@pytest.mark.parametrize("sequence_size", [384])
@pytest.mark.parametrize("use_optimized_version", [True, False])
def test_bert(device, use_program_cache, model_name, batch_size, sequence_size, use_optimized_version):
    enable_persistent_kernel_cache()
    run_bert_question_and_answering_inference(model_name, batch_size, sequence_size, use_optimized_version, device)
    disable_persistent_kernel_cache()
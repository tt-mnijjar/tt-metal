# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import time

from loguru import logger
import torch
from transformers import BloomConfig, BloomForQuestionAnswering, BloomTokenizerFast
import pytest

from models.experimental.functional_bloom.tt import ttnn_functional_bloom
from models.experimental.functional_bloom.tt import ttnn_optimized_functional_bloom
from models.utility_functions import enable_persistent_kernel_cache, disable_persistent_kernel_cache
from models.utility_functions import skip_for_wormhole_b0

import ttnn
from ttnn.model_preprocessing import preprocess_model_parameters

from tests.ttnn.utils_for_testing import assert_with_pcc


@skip_for_wormhole_b0()
@pytest.mark.parametrize("ttnn_model", [ttnn_functional_bloom, ttnn_optimized_functional_bloom])
def test_pcc_of_bloom_for_question_answering(device, use_program_cache, ttnn_model, batch_size=8, max_length=384):
    torch.manual_seed(0)

    model_name = "bigscience/bloom-560m"
    config = BloomConfig.from_pretrained(model_name)
    tokenizer = BloomTokenizerFast.from_pretrained(model_name)
    torch_model = BloomForQuestionAnswering.from_pretrained(model_name).eval()

    num_heads = config.n_head

    question = "What is my name?"
    context = "My name is John."
    inputs = tokenizer.encode_plus(question, context, return_tensors="pt")

    torch_output = torch_model(**inputs)
    torch_start_logits = torch_output.start_logits
    torch_end_logits = torch_output.end_logits

    parameters = preprocess_model_parameters(
        f"ttnn-functional-bloom-for-question-answering",
        initialize_model=lambda: torch_model,
        device=device,
        custom_preprocessor=ttnn_model.custom_preprocessor,
    )

    input_ids = inputs.input_ids
    attention_mask = inputs.attention_mask

    num_tokens = input_ids.shape[-1]
    input_ids = input_ids.expand((batch_size, num_tokens))
    attention_mask = attention_mask.expand((batch_size, num_tokens))

    input_ids, alibi, causal_mask = ttnn_model.preprocess_inputs(
        input_ids=input_ids, device=device, num_heads=num_heads, attention_mask=attention_mask, max_length=max_length
    )

    # Run twice to measure the time with and without the program cache
    tt_output = ttnn_model.bloom_for_question_answering(input_ids, alibi, causal_mask, parameters, num_heads)

    tt_output = ttnn.from_device(tt_output)
    tt_output = ttnn.to_layout(tt_output, ttnn.ROW_MAJOR_LAYOUT)
    tt_output = ttnn.to_torch(tt_output)
    tt_start_logits = tt_output[:1, :num_tokens, 0]
    tt_end_logits = tt_output[:1, :num_tokens, 1]

    if ttnn_model == ttnn_functional_bloom:
        assert_with_pcc(torch_start_logits, tt_start_logits, 0.939)
        assert_with_pcc(torch_end_logits, tt_end_logits, 0.911)
    elif ttnn_model == ttnn_optimized_functional_bloom:
        assert_with_pcc(torch_start_logits, tt_start_logits, 0.88)
        assert_with_pcc(torch_end_logits, tt_end_logits, 0.88)
    else:
        raise RecursionError("Invalid ttnn_model")

    assert torch_start_logits.argmax() == tt_start_logits.argmax()
    assert torch_end_logits.argmax() == tt_end_logits.argmax()


@skip_for_wormhole_b0()
@pytest.mark.parametrize("ttnn_model", [ttnn_functional_bloom, ttnn_optimized_functional_bloom])
def test_performance_of_bloom_for_question_answering(
    device, use_program_cache, ttnn_model, batch_size=8, max_length=384
):
    torch.manual_seed(0)

    enable_persistent_kernel_cache()

    model_name = "bigscience/bloom-560m"
    config = BloomConfig.from_pretrained(model_name)
    tokenizer = BloomTokenizerFast.from_pretrained(model_name)

    num_heads = config.n_head

    question = "What is my name?"
    context = "My name is John."
    inputs = tokenizer.encode_plus(question, context, return_tensors="pt")

    parameters = preprocess_model_parameters(
        "ttnn-functional-bloom-for-question-answering",
        initialize_model=lambda: BloomForQuestionAnswering.from_pretrained(model_name).eval(),
        device=device,
        custom_preprocessor=ttnn_model.custom_preprocessor,
    )

    input_ids = inputs.input_ids
    attention_mask = inputs.attention_mask

    num_tokens = input_ids.shape[-1]
    input_ids = input_ids.expand((batch_size, num_tokens))
    attention_mask = attention_mask.expand((batch_size, num_tokens))

    input_ids, alibi, causal_mask = ttnn_model.preprocess_inputs(
        input_ids=input_ids, device=device, num_heads=num_heads, attention_mask=attention_mask, max_length=max_length
    )

    # Run twice to measure the time with and without the program cache
    for _ in range(2):
        start = time.time()
        tt_output = ttnn_model.bloom_for_question_answering(input_ids, alibi, causal_mask, parameters, num_heads)
        tt_output = ttnn.from_device(tt_output)
        end = time.time()

        batch_size, _ = input_ids.shape
        duration = end - start
        logger.info(f"Duration: {duration}")
        logger.info(f"Samples per second: {1 / duration * batch_size}")

    disable_persistent_kernel_cache()
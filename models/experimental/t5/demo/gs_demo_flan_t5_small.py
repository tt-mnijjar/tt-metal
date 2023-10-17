# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from models.experimental.t5.tt.t5_for_conditional_generation import (
    flan_t5_small_for_conditional_generation,
)
from models.experimental.t5.demo.demo_utils import run_demo_t5


def test_demo_flan_t5_small():
    run_demo_t5(flan_t5_small_for_conditional_generation)
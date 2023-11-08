// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "debug_print.h"

/*
 * Test printing from a kernel running on NCRISC.
*/

void kernel_main() {
    DPRINT_DATA1(DPRINT << "Test Debug Print: Data1" << ENDL());
}
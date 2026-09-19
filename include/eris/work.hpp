// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

using WorkFunction = void (*)(void* context);

// Hands the long half of an interrupt to a place where interrupts are enabled
// and the handler has already returned.
bool schedule_work(WorkFunction function, void* context);
void work_run_pending();
void work_start();
usize work_pending();
u64 work_dropped();

} // namespace eris

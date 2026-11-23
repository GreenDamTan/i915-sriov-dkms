/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PMU_H_
#define _XE_PMU_H_

#include "xe_pmu_types.h"

struct xe_exec_queue;

#if IS_ENABLED(CONFIG_PERF_EVENTS)
int xe_pmu_register(struct xe_pmu *pmu);
void xe_pmu_vf_queue_fini(struct xe_exec_queue *q);
#else
static inline int xe_pmu_register(struct xe_pmu *pmu) { return 0; }
static inline void xe_pmu_vf_queue_fini(struct xe_exec_queue *q) { }
#endif

#endif


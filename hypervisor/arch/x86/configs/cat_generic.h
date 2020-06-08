/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <board.h>
#include <msr.h>

/* The big core (SKL, KBL, WHL, etc) can support L3 CAT only  */
struct platform_clos_info platform_l3_way12_clos_array[4] = {
	{
		.clos_mask = 0xfc0,
		.msr_index = MSR_IA32_L3_MASK_BASE,
	},
	{
		.clos_mask = 0x03f,
		.msr_index = MSR_IA32_L3_MASK_BASE + 1U,
	},
	{
		.clos_mask = 0x03e,
		.msr_index = MSR_IA32_L3_MASK_BASE + 2U,
	},
	{
		.clos_mask = 0x001,
		.msr_index = MSR_IA32_L3_MASK_BASE + 3U,
	},
};

struct platform_clos_info platform_l3_way16_clos_array[4] = {
	{
		.clos_mask = 0xff00,
		.msr_index = MSR_IA32_L3_MASK_BASE,
	},
	{
		.clos_mask = 0x00ff,
		.msr_index = MSR_IA32_L3_MASK_BASE + 1U,
	},
	{
		.clos_mask = 0x00fe,
		.msr_index = MSR_IA32_L3_MASK_BASE + 2U,
	},
	{
		.clos_mask = 0x0001,
		.msr_index = MSR_IA32_L3_MASK_BASE + 3U,
	},
};

uint16_t platform_l2_clos_num = (uint16_t)(sizeof(platform_l2_clos_array)/sizeof(struct platform_clos_info));
uint16_t platform_l3_way12_clos_num = (uint16_t)(sizeof(platform_l3_way12_clos_array)/sizeof(struct platform_clos_info));
uint16_t platform_l3_way16_clos_num = (uint16_t)(sizeof(platform_l3_way16_clos_array)/sizeof(struct platform_clos_info));

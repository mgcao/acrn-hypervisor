/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SHELL_H
#define SHELL_H

#ifdef HV_DEBUG
#define ENABLE_SAMPLE_VMEXIT_INFO

void get_vmexit_profile_per_pcpu(char *str_arg, size_t str_max);
void get_vmexit_details_per_pcpu(char *str_arg, size_t str_max);
void get_vmexit_profile_per_vm(char *str_arg, size_t str_max);
void get_vmexit_details_per_vm(char *str_arg, size_t str_max);
void clear_vmexit_info_buffer(void);




#endif

void shell_init(void);
void shell_kick(void);

#endif /* SHELL_H */

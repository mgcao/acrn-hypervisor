/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <vm.h>
#include <vm_reset.h>
#include <vmcs.h>
#include <vmexit.h>
#include <irq.h>
#include <schedule.h>
#include <profiling.h>
#include <sprintf.h>
#include <trace.h>
#include <logmsg.h>

void vcpu_thread(struct thread_object *obj)
{
#ifdef HV_DEBUG
	uint64_t vmexit_begin = 0UL, vmexit_end = 0UL;
#endif
	struct acrn_vcpu *vcpu = container_of(obj, struct acrn_vcpu, thread_obj);
	uint32_t basic_exit_reason = 0U;
	int32_t ret = 0;

	do {
		if (!is_lapic_pt_enabled(vcpu)) {
			CPU_IRQ_DISABLE();
		}

		/* Don't open interrupt window between here and vmentry */
		if (need_reschedule(pcpuid_from_vcpu(vcpu))) {
			schedule();
		}

		/* Check and process pending requests(including interrupt) */
		ret = acrn_handle_pending_request(vcpu);
		if (ret < 0) {
			pr_fatal("vcpu handling pending request fail");
			zombie_vcpu(vcpu, VCPU_ZOMBIE);
			/* Fatal error happened (triple fault). Stop the vcpu running. */
			continue;
		}

		reset_event(&vcpu->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
		profiling_vmenter_handler(vcpu);

#ifdef HV_DEBUG
		vmexit_end = rdtsc();
		if (vmexit_begin != 0UL) {
			uint64_t delta = vmexit_end - vmexit_begin;
			uint32_t us = (uint32_t)ticks_to_us(delta);
			uint16_t fls = (uint16_t)(fls32(us) + 1); /* to avoid us = 0 case, then fls=0xFFFF */
			uint16_t index = 0;

			if (fls >= MAX_VMEXIT_LEVEL) {
				index = MAX_VMEXIT_LEVEL - 1;
			} else if (fls > 0) { //if fls == 0, it means the us = 0
				index = fls - 1;
			}

			get_cpu_var(vmexit_cnt)[basic_exit_reason][index]++;
			get_cpu_var(vmexit_time)[basic_exit_reason][0] += delta;

			vcpu->vm->vmexit_cnt[basic_exit_reason][index]++;
			vcpu->vm->vmexit_time[basic_exit_reason][0] += delta;

			if (us > get_cpu_var(vmexit_time)[basic_exit_reason][1]) {
				get_cpu_var(vmexit_time)[basic_exit_reason][1] = us;
			}

			if (us > vcpu->vm->vmexit_time[basic_exit_reason][1]) {
				vcpu->vm->vmexit_time[basic_exit_reason][1] = us;
			}
		}
#endif
		TRACE_2L(TRACE_VM_ENTER, 0UL, 0UL);
		ret = run_vcpu(vcpu);
		if (ret != 0) {
			pr_fatal("vcpu resume failed");
			zombie_vcpu(vcpu, VCPU_ZOMBIE);
			/* Fatal error happened (resume vcpu failed). Stop the vcpu running. */
			continue;
		}
		basic_exit_reason = vcpu->arch.exit_reason & 0xFFFFU;
		TRACE_2L(TRACE_VM_EXIT, basic_exit_reason, vcpu_get_rip(vcpu));

#ifdef HV_DEBUG
		vmexit_begin = rdtsc();
		get_cpu_var(vmexit_cnt)[basic_exit_reason][TOTAL_ARRAY_LEVEL - 1]++;
		vcpu->vm->vmexit_cnt[basic_exit_reason][TOTAL_ARRAY_LEVEL - 1]++;
#endif
		vcpu->arch.nrexits++;

		profiling_pre_vmexit_handler(vcpu);

		if (!is_lapic_pt_enabled(vcpu)) {
			CPU_IRQ_ENABLE();
		}
		/* Dispatch handler */
		ret = vmexit_handler(vcpu);
		if (ret < 0) {
			pr_fatal("dispatch VM exit handler failed for reason"
				" %d, ret = %d!", basic_exit_reason, ret);
			vcpu_inject_gp(vcpu, 0U);
			continue;
		}

		profiling_post_vmexit_handler(vcpu);
	} while (1);
}

void default_idle(__unused struct thread_object *obj)
{
	uint16_t pcpu_id = get_pcpu_id();

	while (1) {
		if (need_reschedule(pcpu_id)) {
			schedule();
		} else if (need_offline(pcpu_id)) {
			cpu_dead();
		} else if (need_shutdown_vm(pcpu_id)) {
			shutdown_vm_from_idle(pcpu_id);
		} else {
			CPU_IRQ_ENABLE();
			cpu_do_idle();
			CPU_IRQ_DISABLE();
		}
	}
}

void run_idle_thread(void)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct thread_object *idle = &per_cpu(idle, pcpu_id);
	char idle_name[16];

	snprintf(idle_name, 16U, "idle%hu", pcpu_id);
	(void)strncpy_s(idle->name, 16U, idle_name, 16U);
	idle->pcpu_id = pcpu_id;
	idle->thread_entry = default_idle;
	idle->switch_out = NULL;
	idle->switch_in = NULL;

	run_thread(idle);

	/* Control should not come here */
	cpu_dead();
}

#ifdef HV_DEBUG
void get_vmexit_profile_per_pcpu(char *str_arg, size_t str_max)
{
	char *str = str_arg;
	uint16_t cpu, i;
	size_t len, size = str_max;
	uint16_t pcpu_num = get_pcpu_nums();

	len = snprintf(str, size, "\r\nNow(us) = %16lld\r\n", ticks_to_us(rdtsc()));
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	len = snprintf(str, size, "\r\nREASON");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (cpu = 0U; cpu < pcpu_num; cpu++) {
		len = snprintf(str, size, "\t      CPU%hu\t        US", cpu);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;
	}

	for (i = 0U; i < 64U; i++) {

		/* to ignore the 0 count vmexit */
		for (cpu = 0U; cpu < pcpu_num; cpu++) {
			if (per_cpu(vmexit_cnt, cpu)[i][TOTAL_ARRAY_LEVEL - 1] != 0)
				break;
		}

		if (cpu == pcpu_num)
			continue;

		len = snprintf(str, size, "\r\n0x%02x", i);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;


		for (cpu = 0U; cpu < pcpu_num; cpu++) {
			len = snprintf(str, size, "\t%10lld\t%10lld", per_cpu(vmexit_cnt, cpu)[i][TOTAL_ARRAY_LEVEL - 1],
				ticks_to_us(per_cpu(vmexit_time, cpu)[i][0]));
			if (len >= size) {
				goto overflow;
			}

			size -= len;
			str += len;
		}
	}

	for (cpu = 0U; cpu < pcpu_num; cpu++) {
		len = snprintf(str, size, "\r\ncpu%d thread resched: %ld", cpu, per_cpu(resched_times, cpu));

		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;
	}

	snprintf(str, size, "\r\n");
	return;

overflow:
	printf("buffer size could not be enough! please check!\n");
}

void get_vmexit_profile_per_vm(char *str_arg, size_t str_max)
{
	char *str = str_arg;
	uint16_t vm_id, i;
	size_t len, size = str_max;
	struct acrn_vm *vm;

	len = snprintf(str, size, "\r\nNow(us) = %16lld; total vmexit per vm (count & time):\r\n",
		ticks_to_us(rdtsc()));
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	len = snprintf(str, size, "\r\nREASON");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		if (!is_poweroff_vm(vm)) {
			len = snprintf(str, size, "\t      VM%hu\t        US", vm_id);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;
		}
	}

	for (i = 0U; i < 64U; i++) {

		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (!is_poweroff_vm(vm)) {
				if (vm->vmexit_cnt[i][TOTAL_ARRAY_LEVEL - 1] != 0)
					break;
			}
		}

		if (vm_id == CONFIG_MAX_VM_NUM)
			continue;

		len = snprintf(str, size, "\r\n0x%02x", i);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (!is_poweroff_vm(vm)) {
				len = snprintf(str, size, "\t%10lld\t%10lld", vm->vmexit_cnt[i][TOTAL_ARRAY_LEVEL - 1],
					ticks_to_us(vm->vmexit_time[i][0]));
				if (len >= size) {
					goto overflow;
				}

				size -= len;
				str += len;
			}
		}
	}

	snprintf(str, size, "\r\n");
	return;

overflow:
	printf("buffer size could not be enough! please check!\n");
}

static const char *level_info[MAX_VMEXIT_LEVEL] = {
	"   0us -   2us",
	"   2us -   4us",
	"   4us -   8us",
	"   8us -  16us",
	"  16us -  32us",
	"  32us -  64us",
	"  64us - 128us",
	" 128us - 256us",
	" 256us - 512us",
	" 512us -1024us",
	"1024us -2048us",
	"2048us -4096us",
	"4096us -8192us",
	"8192us -  more",
};

void get_vmexit_details_per_pcpu(char *str_arg, size_t str_max)
{
	char *str = str_arg;
	uint16_t cpu, i, level;
	size_t len, size = str_max;
	uint16_t pcpu_num = get_pcpu_nums();

	len = snprintf(str, size, "\r\nNow=%lldus, for detailed latency of each vmexit on each cpu:",
		ticks_to_us(rdtsc()));
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (i = 0U; i < 64U; i++) {

		/* to ignore the 0 count vmexit */
		for (cpu = 0U; cpu < pcpu_num; cpu++) {
			if (per_cpu(vmexit_cnt, cpu)[i][TOTAL_ARRAY_LEVEL - 1] != 0)
				break;
		}

		if (cpu == pcpu_num)
			continue;

		len = snprintf(str, size, "\r\n\r\n   VMEXIT/0x%02x", i);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		for (cpu = 0U; cpu < pcpu_num; cpu++) {
			len = snprintf(str, size, "        CPU%hu", cpu);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;
		}

		for (level = 0; level < MAX_VMEXIT_LEVEL; level++) {

			/* to ignore the 0 count vmexit */
			for (cpu = 0U; cpu < pcpu_num; cpu++) {
				if (per_cpu(vmexit_cnt, cpu)[i][level] != 0)
					break;
			}

			if (cpu == pcpu_num)
				continue;

			len = snprintf(str, size, "\r\n%s", level_info[level]);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;

			for (cpu = 0U; cpu < pcpu_num; cpu++) {
				len = snprintf(str, size, "%12lld", per_cpu(vmexit_cnt, cpu)[i][level]);
				if (len >= size) {
					goto overflow;
				}
			
				size -= len;
				str += len;
			}
		}

		len = snprintf(str, size, "\r\n  Max Lat(us):");
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		for (cpu = 0U; cpu < pcpu_num; cpu++) {
			len = snprintf(str, size, "%12lld", per_cpu(vmexit_time, cpu)[i][1]);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;
		}

		
	}

	snprintf(str, size, "\r\n");
	return;

overflow:
	printf("buffer size could not be enough! please check!\n");
}


void get_vmexit_details_per_vm(char *str_arg, size_t str_max)
{
	char *str = str_arg;
	uint16_t vm_id, i, level;
	size_t len, size = str_max;
	struct acrn_vm *vm;

	len = snprintf(str, size, "\r\nNow=%lldus, for detailed latency of each vmexit on each VM:",
		ticks_to_us(rdtsc()));
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (i = 0U; i < 64U; i++) {

		/* to ignore the 0 count vmexit */
		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (!is_poweroff_vm(vm)) {
				if (vm->vmexit_cnt[i][TOTAL_ARRAY_LEVEL - 1] != 0)
					break;
			}
		}

		if (vm_id == CONFIG_MAX_VM_NUM)
			continue;

		len = snprintf(str, size, "\r\n\r\n   VMEXIT/0x%02x", i);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (!is_poweroff_vm(vm)) {
				len = snprintf(str, size, "         VM%hu", vm_id);
				if (len >= size) {
					goto overflow;
				}
				size -= len;
				str += len;
			}
		}

		for (level = 0; level < MAX_VMEXIT_LEVEL; level++) {

			/* to ignore the 0 count vmexit */
			for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
				vm = get_vm_from_vmid(vm_id);
				if (!is_poweroff_vm(vm)) {
					if (vm->vmexit_cnt[i][level] != 0)
						break;
				}
			}

			if (vm_id == CONFIG_MAX_VM_NUM)
				continue;

			len = snprintf(str, size, "\r\n%s", level_info[level]);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;

			for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
				vm = get_vm_from_vmid(vm_id);
				if (!is_poweroff_vm(vm)) {
					len = snprintf(str, size, "%12lld", vm->vmexit_cnt[i][level]);
					if (len >= size) {
						goto overflow;
					}

					size -= len;
					str += len;
				}
			}

		}

		len = snprintf(str, size, "\r\n  Max Lat(us):");
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (!is_poweroff_vm(vm)) {
				len = snprintf(str, size, "%12lld", vm->vmexit_time[i][1]);
				if (len >= size) {
					goto overflow;
				}
				size -= len;
				str += len;
			}
		}

	}

	snprintf(str, size, "\r\n");
	return;

overflow:
	printf("buffer size could not be enough! please check!\n");
}

void clear_vmexit_info_buffer(void)
{
	uint16_t cpu, vm_id;
	uint16_t pcpu_num = get_pcpu_nums();
	struct acrn_vm *vm;

	for (cpu = 0U; cpu < pcpu_num; cpu++) {
		memset(per_cpu(vmexit_cnt, cpu), 0, sizeof(per_cpu(vmexit_cnt, cpu)));
		memset(per_cpu(vmexit_time, cpu), 0, sizeof(per_cpu(vmexit_time, cpu)));
	}

	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		if (!is_poweroff_vm(vm)) {
			memset(vm->vmexit_cnt, 0, sizeof(vm->vmexit_cnt));
			memset(vm->vmexit_time, 0, sizeof(vm->vmexit_time));
		}
	}

}
#endif /* HV_DEBUG */

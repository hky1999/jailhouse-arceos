#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>

#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/smp.h>

#include "axtask.h"
#include "main.h"
#include "cell.h"

#include <jailhouse/hypercall.h>

static cpumask_t offlined_cpus;

int jailhouse_cmd_axtask_up(struct jailhouse_axtask_up __user *arg)
{
	unsigned int cpu;
	unsigned long phys_addr;
	unsigned long size = PAGE_SIZE;
	struct jailhouse_axtask_up up_config;
    int cpu_mask; 
	int err = 0;
	unsigned int cpu_id;
	int type;
	void* addr;
	void* size_addr;
	void* content_addr;
	void __user *user_addr;
	int i;

	/* Off-line each CPU assigned to the axtask and remove it from the
	 * root cell's set. */
	if (copy_from_user(&up_config, arg, sizeof(up_config)))
		return -EFAULT;
	cpu_mask = up_config.cpu_mask;
	cpu_id = smp_processor_id();
	
    for (cpu = 0; cpu < sizeof(cpu_mask) * 8; cpu++) {
        if (cpu_mask & (1 << cpu)) {
            if (cpu_online(cpu)) {
				err = cpu_down(cpu);
				pr_err("cpu %d is down err:%d\n", cpu, err);
				if (err)
					goto error_cpu_online;
				cpumask_set_cpu(cpu, &offlined_cpus);
			}
			cpumask_clear_cpu(cpu, &root_cell->cpus_assigned);
        }
    }
	type = up_config.type;
	for(i = 0; i < JAILHOUSE_FILE_MAXNUM; ++i) {
		size += (up_config.size[i] + PAGE_SIZE - 1) & PAGE_MASK;
	}
	pr_err("size: %lx\n", size);

	addr = kmalloc(size, GFP_USER | __GFP_NOWARN);
	size_addr = addr;
	*(unsigned long *)size_addr = size;
	size_addr += sizeof(unsigned long);
	content_addr = addr + PAGE_SIZE;
	for(i = 0; i < JAILHOUSE_FILE_MAXNUM; ++i) {
		*(unsigned long *)size_addr = (up_config.size[i] + PAGE_SIZE - 1) & PAGE_MASK;
		size_addr += sizeof(unsigned long);
		user_addr = (void __user *)up_config.addr[i];
		if (copy_from_user(content_addr, user_addr, up_config.size[i]))
			return -EFAULT;
		content_addr += (up_config.size[i] + PAGE_SIZE - 1) & PAGE_MASK;
	}

	phys_addr = __pa(addr);
	pr_err("Virtual address: %px, Physical address: %lx\n", addr, phys_addr);
	pr_err("[jailhouse_cmd_axtask_up] current cpu:%d cpu_mask:%d type:%d phys_addr:%lx\n", cpu_id, cpu_mask, type, phys_addr);
    err = jailhouse_call_arg3(JAILHOUSE_HC_AXTASK_UP, cpu_mask, (unsigned long)type, phys_addr);
	if (err < 0)
		goto error_cpu_online;
	
	return err;

error_cpu_online:
	pr_err("create axtask failed err:%d\n", err);
	for (cpu = 0; cpu < sizeof(cpu_mask) * 8; cpu++) {
        if (cpu_mask & (1 << cpu))  {
			if (!cpu_online(cpu) && cpu_up(cpu) == 0)
				cpumask_clear_cpu(cpu, &offlined_cpus);
			cpumask_set_cpu(cpu, &root_cell->cpus_assigned);
		}
	}
	return err;
}

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
	struct jailhouse_axtask_up up_config;
    int cpu_mask; 
	int err = 0;
	unsigned int cpu_id;
	int type;
	void* addr;
	void __user *user_addr;


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
	user_addr = (void __user *)(unsigned long)up_config.addr;
	addr = kmalloc(up_config.size, GFP_USER | __GFP_NOWARN);

	if (copy_from_user(addr, user_addr, up_config.size))
		return -EFAULT;
	
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

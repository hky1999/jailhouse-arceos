#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>

#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/smp.h>


#include "axprocess.h"
#include "main.h"
#include "cell.h"

#include <jailhouse/hypercall.h>

static cpumask_t offlined_cpus;

int jailhouse_cmd_axprocess_up(struct jailhouse_axprocess_up __user *arg)
{
	unsigned int cpu;
	struct jailhouse_axprocess_up up_config;
    int cpu_mask;
	int err;
	unsigned int cpu_id;


	/* Off-line each CPU assigned to the axprocess and remove it from the
	 * root cell's set. */
	if (copy_from_user(&up_config, arg, sizeof(up_config)))
		return -EFAULT;
	cpu_mask = up_config.cpu_mask;
	cpu_id = smp_processor_id();
	pr_err("[jailhouse_cmd_axprocess_up] current cpu:%d cpu_mask:%d\n", cpu_id, cpu_mask);
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
    
    err = jailhouse_call_arg1(JAILHOUSE_HC_AXPROCESS_UP, cpu_mask);
	if (err < 0)
		goto error_cpu_online;
	
	return err;

error_cpu_online:
	pr_err("create axprocess failed err:%d\n", err);
	for (cpu = 0; cpu < sizeof(cpu_mask) * 8; cpu++) {
        if (cpu_mask & (1 << cpu))  {
			if (!cpu_online(cpu) && cpu_up(cpu) == 0)
				cpumask_clear_cpu(cpu, &offlined_cpus);
			cpumask_set_cpu(cpu, &root_cell->cpus_assigned);
		}
	}
	return err;
}

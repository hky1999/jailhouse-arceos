#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>

#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/smp.h>

#include "axvm.h"
#include "main.h"
#include "cell.h"

#include <jailhouse/hypercall.h>

static cpumask_t offlined_cpus;


/// @brief Load image from user address to target physical address provided by arceos-hv.
/// @param image : Here we reuse the jailhouse_preload_image structure from Jailhouse.
///		image->source_address: user address.
///		image->size: image size.
///		image->target_address: target physical address provided by arceos-hv.
int arceos_axvm_load_image(struct jailhouse_preload_image *image) 
{
	void *image_mem;
	int err = 0;

	__u64 page_offs, phys_start;

	phys_start = image->target_address & PAGE_MASK;
	page_offs = offset_in_page(image->target_address);
	
	pr_info("[%s]:\n", __func__);

	image_mem = jailhouse_ioremap(phys_start, 0,
			PAGE_ALIGN(image->size + page_offs));
	
	pr_info("phys_start 0x%llx remap to 0x%p\n", phys_start, image_mem);

	if (!image_mem) {
		pr_err("jailhouse: Unable to map cell RAM at %08llx "
		       "for image loading\n",
		       (unsigned long long)(image->target_address));
		return -EBUSY;
	}

	pr_info("copy to 0x%p size 0x%llx, loading...\n", image_mem + page_offs, image->size);

	if (copy_from_user(image_mem + page_offs,
			   (void __user *)(unsigned long)image->source_address,
			   image->size)) {
		pr_err("jailhouse: Unable to copy image from user %08llx "
		       "for image loading\n",
		       (unsigned long long)(image->source_address));
		err = -EFAULT;
	}
		
	/*
	 * ARMv7 and ARMv8 require to clean D-cache and invalidate I-cache for
	 * memory containing new instructions. On x86 this is a NOP.
	 */
	flush_icache_range((unsigned long)(image_mem + page_offs),
			   (unsigned long)(image_mem + page_offs) + image->size);
#ifdef CONFIG_ARM
	/*
	 * ARMv7 requires to flush the written code and data out of D-cache to
	 * allow the guest starting off with caches disabled.
	 */
	__cpuc_flush_dcache_area(image_mem + page_offs, image->size);
#endif

	vunmap(image_mem);

	return err;
}

/// @brief Create axvm config through HVC.
/// @param arg : Pointer to the user-provided VM creation information..
///		`jailhouse_axvm_create` need to be refactored.
int arceos_cmd_axvm_create(struct jailhouse_axvm_create __user *arg)
{
	unsigned int cpu;
	struct jailhouse_axvm_create vm_cfg;
    int cpu_mask; 
	int err = 0;
	unsigned int cpu_id;
	int vm_id = 0;

	unsigned long arg_phys_addr;
	struct arceos_axvm_create_arg* arceos_hvc_axvm_create;
	void *raw_config_file;
	struct jailhouse_preload_image bios_image;
	struct jailhouse_preload_image kernel_image;
	struct jailhouse_preload_image ramdisk_image;

	if (copy_from_user(&vm_cfg, arg, sizeof(vm_cfg)))
		return -EFAULT;

	vm_id = vm_cfg.id;
	cpu_mask = vm_cfg.cpu_set;
	cpu_id = smp_processor_id();
	
	/* Off-line each CPU assigned to the axvm and remove it from the
	 * root cell's set. */
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

	arceos_hvc_axvm_create = kmalloc(sizeof(struct arceos_axvm_create_arg), GFP_USER | __GFP_NOWARN);
	raw_config_file = kmalloc(vm_cfg.raw_cfg_file_size, GFP_USER | __GFP_NOWARN);
	if (copy_from_user(raw_config_file,(const void __user *) vm_cfg.raw_cfg_file_ptr, vm_cfg.raw_cfg_file_size)) {
		pr_err("Failed to copy raw_config_file\n");
		return -EFAULT;
	}
		
	arceos_hvc_axvm_create->vm_id = vm_cfg.id;
	arceos_hvc_axvm_create->raw_cfg_base = __pa(raw_config_file);
	arceos_hvc_axvm_create->raw_cfg_size = vm_cfg.raw_cfg_file_size;
	// arceos_hvc_axvm_create->vm_type = vm_cfg.type;
	// arceos_hvc_axvm_create->cpu_mask = cpu_mask;

	// // This field should be set by user, but now this is provided by hypervisor.
	// arceos_hvc_axvm_create->vm_entry_point = 0xdeadbeef;
	// // This field should be set by user, but now this is provided by hypervisor.
	// arceos_hvc_axvm_create->bios_load_gpa = 0xdeadbeef;
	// // This field should be set by user, but now this is provided by hypervisor.
	// arceos_hvc_axvm_create->kernel_load_gpa = 0xdeadbeef;
	// // This field should be set by user, but now this is provided by hypervisor.
	// arceos_hvc_axvm_create->ramdisk_load_gpa = 0xdeadbeef;

	// This field should be set by hypervisor.
	arceos_hvc_axvm_create->bios_load_hpa = 0xdeadbeef;
	// This field should be set by hypervisor.
	arceos_hvc_axvm_create->kernel_load_hpa = 0xdeadbeef;
	// This field should be set by hypervisor.
	arceos_hvc_axvm_create->ramdisk_load_hpa = 0xdeadbeef;

	arg_phys_addr = __pa(arceos_hvc_axvm_create);

	pr_info("[%s] current cpu:%d cpu_mask:%d\n", __func__, cpu_id, cpu_mask);

    err = jailhouse_call_arg1(ARCEOS_HC_AXVM_CREATE_CFG, arg_phys_addr);
	if (err < 0) {
		pr_err("[%s] Failed in JAILHOUSE_AXVM_CREATE\n", __func__);
		goto error_cpu_online;
	}
	
	pr_info("[%s] JAILHOUSE_AXVM_CREATE VM %d success\n", 
		__func__, (int) arceos_hvc_axvm_create->vm_id);
	// pr_info("[%s] VM [%d] vm_entry_point 0x%llx\n", 
	// 	__func__, (int) arceos_hvc_axvm_create->vm_id, arceos_hvc_axvm_create->vm_entry_point);
	// pr_info("[%s] VM [%d] bios_load_gpa 0x%llx\n", 
	// 	__func__, (int) arceos_hvc_axvm_create->vm_id, arceos_hvc_axvm_create->bios_load_gpa);
	// pr_info("[%s] VM [%d] kernel_load_gpa 0x%llx\n", 
	// 	__func__, (int) arceos_hvc_axvm_create->vm_id, arceos_hvc_axvm_create->kernel_load_gpa);
	// pr_info("[%s] VM [%d] ramdisk_load_gpa 0x%llx\n", 
	// 	__func__, (int) arceos_hvc_axvm_create->vm_id, arceos_hvc_axvm_create->ramdisk_load_gpa);
	vm_id = (int) arceos_hvc_axvm_create->vm_id;

	// Load BIOS image
	bios_image.source_address = vm_cfg.bios_img_ptr;
	bios_image.size = vm_cfg.bios_img_size;
	bios_image.target_address = arceos_hvc_axvm_create->bios_load_hpa;
	bios_image.padding = 0;

	pr_info("[%s] bios_load_hpa: 0x%llx\n", __func__, arceos_hvc_axvm_create->bios_load_hpa);

	err = arceos_axvm_load_image(&bios_image);
	if (err < 0) {
		pr_err("[%s] Failed in arceos_axvm_load_image bios_image\n", __func__);
		goto error_cpu_online;
	}
	
	// Load kernel image
	kernel_image.source_address = vm_cfg.kernel_img_ptr;
	kernel_image.size = vm_cfg.kernel_img_size;
	kernel_image.target_address = arceos_hvc_axvm_create->kernel_load_hpa;
	kernel_image.padding = 0;

	pr_info("[%s] kernel_load_hpa: 0x%llx\n", __func__, arceos_hvc_axvm_create->kernel_load_hpa);

	err = arceos_axvm_load_image(&kernel_image);
	if (err < 0) {
		pr_err("[%s] Failed in arceos_axvm_load_image kernel_image\n", __func__);
		goto error_cpu_online;
	}

	// Load ramdisk image
	if (vm_cfg.ramdisk_img_size != 0) {
		ramdisk_image.source_address = vm_cfg.ramdisk_img_ptr;
		ramdisk_image.size = vm_cfg.ramdisk_img_size;
		ramdisk_image.target_address = arceos_hvc_axvm_create->ramdisk_load_hpa;
		ramdisk_image.padding = 0;

		pr_info("[%s] ramdisk_load_hpa: 0x%llx\n", __func__, arceos_hvc_axvm_create->ramdisk_load_hpa);

		err = arceos_axvm_load_image(&ramdisk_image);
		if (err < 0) {
			pr_err("[%s] Failed in arceos_axvm_load_image ramdisk_image\n", __func__);
			goto error_cpu_online;
		}
	}

	// pr_err("[%s] images load success, booting VM %d\n", __func__, vm_id);

	// err = jailhouse_call_arg1(ARCEOS_HC_AXVM_BOOT, (unsigned long)vm_id);

	kfree(arceos_hvc_axvm_create);

	return err;

error_cpu_online:
	pr_err("create axvm failed err:%d\n", err);
	for (cpu = 0; cpu < sizeof(cpu_mask) * 8; cpu++) {
        if (cpu_mask & (1 << cpu))  {
			if (!cpu_online(cpu) && cpu_up(cpu) == 0)
				cpumask_clear_cpu(cpu, &offlined_cpus);
			cpumask_set_cpu(cpu, &root_cell->cpus_assigned);
		}
	}
	kfree(arceos_hvc_axvm_create);
	return err;
}

/// @brief Boot guest VM.
/// @param arg : Pointer to the user-provided VM creation information..
///		`jailhouse_axvm_create` need to be refactored.
int arceos_cmd_axvm_boot(struct jailhouse_axvm_boot __user *arg) 
{
	int err = 0;
	
	struct jailhouse_axvm_boot boot_arg;

	if (copy_from_user(&boot_arg, arg, sizeof(boot_arg)))
		return -EFAULT;

	pr_err("%s Booting VM [%lld]\n", __func__, boot_arg.id);

	err = jailhouse_call_arg1(ARCEOS_HC_AXVM_BOOT, boot_arg.id);
	return err;
}

/// @brief Boot guest VM.
/// @param arg : Pointer to the user-provided VM creation information..
///		`jailhouse_axvm_create` need to be refactored.
int arceos_cmd_axvm_shutdown(struct jailhouse_axvm_shutdown __user *arg) 
{
	int err = 0;
	
	struct jailhouse_axvm_boot shutdown_arg;

	if (copy_from_user(&shutdown_arg, arg, sizeof(shutdown_arg)))
		return -EFAULT;

	pr_err("%s Shutdown VM [%lld].\n\n unimplement\n", __func__, shutdown_arg.id);

	// err = jailhouse_call_arg1(ARCEOS_HC_AXVM_BOOT, vmid);
	return err;
}

#ifndef _JAILHOUSE_DRIVER_MAIN_H
#define _JAILHOUSE_DRIVER_MAIN_H

#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/uaccess.h>

#include "jailhouse.h"

int jailhouse_cmd_axprocess_up(struct jailhouse_axprocess_up __user *arg);

#endif /* !_JAILHOUSE_DRIVER_MAIN_H */
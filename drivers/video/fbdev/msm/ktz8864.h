/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KTZ8864_H_
#define _KTZ8864_H_

#include <linux/types.h>

int tkz8864_set_bl(int level);
void tkz8864_bias_supply_en(int enable);
int tkz8864_gpio_en(int enable);
bool tkz8864_is_ready(void);

#endif /* _KTZ8864_H_ */

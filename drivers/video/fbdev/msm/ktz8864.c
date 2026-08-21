/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kinetic KTZ8864 backlight + LCD bias I2C driver
 * Reconstructed from EEBBK SDM660 OEM kernel 4.4.153-perf
 *
 * DT bindings (unchanged from OEM):
 *   compatible = "ti,ktz8864";
 *   reg = <0x11>;
 *   ktz8864-en-gpio = <...>;
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "ktz8864.h"

#define KTZ8864_BL_CFG1		0x02
#define KTZ8864_BL_CFG2		0x03
#define KTZ8864_BL_BRT_LSB	0x04
#define KTZ8864_BL_BRT_MSB	0x05
#define KTZ8864_BL_EN		0x08
#define KTZ8864_LCD_BIAS_CFG1	0x09
#define KTZ8864_LCD_BIAS_CFG2	0x0A
#define KTZ8864_LCD_BIAS_CFG3	0x0B
#define KTZ8864_LCD_BOOST_CFG	0x0C
#define KTZ8864_OUTP_CFG	0x0D
#define KTZ8864_OUTN_CFG	0x0E

/* OEM observed limits from mdss_dsi_panel_bklt_tkz8864 / tkz8864_set_bl */
#define KTZ8864_BRIGHTNESS_MAX	0x47B	/* 1147 */
#define KTZ8864_BRIGHTNESS_MIN	0x5A	/* 90; 1..89 rejected by OEM */
#define KTZ8864_BL_EN_ON	0x1F	/* 4 sinks + BL_EN */
#define KTZ8864_REG_MAX		0x15

static struct i2c_client *ktz8864_client;
static int tkz8864_en_gpio = -1;
static DEFINE_MUTEX(ktz8864_lock);

static int ktz8864_write(struct i2c_client *client, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};
	int ret, retry = 3;

	mutex_lock(&ktz8864_lock);
	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1) {
			mutex_unlock(&ktz8864_lock);
			return 0;
		}
	} while (--retry > 0);
	mutex_unlock(&ktz8864_lock);

	pr_err("%s: hgc_i2c_transfer(write) error, ret = %d\n",
	       __func__, ret);
	return ret < 0 ? ret : -EIO;
}

static int ktz8864_read(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msgs[2];
	int ret, retry = 3;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = val;

	mutex_lock(&ktz8864_lock);
	do {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2) {
			mutex_unlock(&ktz8864_lock);
			return 0;
		}
	} while (--retry > 0);
	mutex_unlock(&ktz8864_lock);

	pr_err("[IIC]: hgc_i2c_transfer(read) error, ret=%d!!\n", ret);
	return ret < 0 ? ret : -EIO;
}

int tkz8864_set_bl(int level)
{
	struct i2c_client *client = ktz8864_client;
	int enable;

	if (level > KTZ8864_BRIGHTNESS_MAX)
		return -EINVAL;

	pr_debug("hgc->value = %d\n", level);

	enable = (level != 0);
	if (enable && level <= KTZ8864_BRIGHTNESS_MIN)
		return -EINVAL;

	if (!client)
		return 0;

	if (enable) {
		ktz8864_write(client, KTZ8864_BL_BRT_LSB, level & 0x7);
		ktz8864_write(client, KTZ8864_BL_BRT_MSB, (level >> 3) & 0xFF);
		ktz8864_write(client, KTZ8864_BL_EN, KTZ8864_BL_EN_ON);
	} else {
		ktz8864_write(client, KTZ8864_BL_EN, 0x00);
	}

	return 0;
}
EXPORT_SYMBOL(tkz8864_set_bl);

void tkz8864_bias_supply_en(int enable)
{
	struct i2c_client *client = ktz8864_client;

	pr_info("tkz8864_bias_supply_en = %d\n", enable);

	if (!client)
		return;

	if (enable) {
		ktz8864_write(client, KTZ8864_LCD_BIAS_CFG2, 0x11);
		ktz8864_write(client, KTZ8864_LCD_BIAS_CFG3, 0x00);
		ktz8864_write(client, KTZ8864_LCD_BOOST_CFG, 0x2A);
		ktz8864_write(client, KTZ8864_OUTP_CFG, 0x24);
		ktz8864_write(client, KTZ8864_OUTN_CFG, 0x24);
		ktz8864_write(client, KTZ8864_LCD_BIAS_CFG1, 0x9C);
		mdelay(5);
		ktz8864_write(client, KTZ8864_LCD_BIAS_CFG1, 0x9E);
	} else {
		ktz8864_write(client, KTZ8864_OUTP_CFG, 0x0E);
		ktz8864_write(client, KTZ8864_OUTN_CFG, 0x0E);
		ktz8864_write(client, KTZ8864_LCD_BIAS_CFG1, 0x9E);
		ktz8864_write(client, KTZ8864_LCD_BOOST_CFG, 0x14);
		mdelay(5);
		ktz8864_write(client, KTZ8864_LCD_BIAS_CFG1, 0x9C);
		mdelay(5);
		ktz8864_write(client, KTZ8864_LCD_BIAS_CFG1, 0x98);
	}
}
EXPORT_SYMBOL(tkz8864_bias_supply_en);

int tkz8864_gpio_en(int enable)
{
	if (!gpio_is_valid(tkz8864_en_gpio))
		return -EINVAL;

	gpio_set_value(tkz8864_en_gpio, !!enable);
	return 0;
}
EXPORT_SYMBOL(tkz8864_gpio_en);

bool tkz8864_is_ready(void)
{
	return ktz8864_client != NULL;
}
EXPORT_SYMBOL(tkz8864_is_ready);

static ssize_t reg_data_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = ktz8864_client;
	int i, len = 0;
	u8 val;

	if (!client)
		return -ENODEV;

	for (i = 0; i <= KTZ8864_REG_MAX; i++) {
		if (ktz8864_read(client, i, &val))
			break;
		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "tkz8864[0x%02x]:0x%02x\n", i, val);
	}
	return len;
}

static DEVICE_ATTR(reg_data, 0444, reg_data_show, NULL);

static int ktz8864_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	u8 val;
	int ret, i;

	pr_info("ktz8864_probe: info==>name = %s addr = 0x%x\n",
		client->name, client->addr);

	tkz8864_en_gpio = of_get_named_gpio(np, "ktz8864-en-gpio", 0);
	if (!gpio_is_valid(tkz8864_en_gpio)) {
		pr_err("%s: tkz8864_en_gpio not specified\n", __func__);
	} else {
		ret = gpio_request(tkz8864_en_gpio, "tkz8864_enable");
		if (ret) {
			pr_err("request tkz8864_enable gpio failed, ret=%d\n",
			       ret);
		} else {
			gpio_direction_output(tkz8864_en_gpio, 1);
			gpio_set_value(tkz8864_en_gpio, 1);
		}
	}

	/* HW settle after enable */
	mdelay(10);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("hgc I2C not supported\n");
		return -ENODEV;
	}

	ktz8864_client = client;

	ret = device_create_file(&client->dev, &dev_attr_reg_data);
	if (ret)
		pr_err("ktz8864_probe, create attribute err = %d\n", ret);

	/* Dump register map 0x00..0x15 like OEM probe */
	for (i = 0; i <= KTZ8864_REG_MAX; i++) {
		if (ktz8864_read(client, i, &val)) {
			pr_err("ktz8864_probe, i2c read invalid!!\n");
			break;
		}
		pr_info("tkz8864 [0x%02x] = 0x%02x\n", i, val);
	}

	return 0;
}

static int ktz8864_remove(struct i2c_client *client)
{
	pr_info("ktz8864_remove\n");
	device_remove_file(&client->dev, &dev_attr_reg_data);
	if (gpio_is_valid(tkz8864_en_gpio)) {
		gpio_set_value(tkz8864_en_gpio, 0);
		gpio_free(tkz8864_en_gpio);
		tkz8864_en_gpio = -1;
	}
	if (ktz8864_client == client)
		ktz8864_client = NULL;
	return 0;
}

static const struct i2c_device_id ktz8864_id[] = {
	{ "ktz8864", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ktz8864_id);

static const struct of_device_id ktz8864_match_table[] = {
	{ .compatible = "ti,ktz8864" },
	{}
};
MODULE_DEVICE_TABLE(of, ktz8864_match_table);

static struct i2c_driver ktz8864_driver = {
	.driver = {
		.name = "ktz8864",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ktz8864_match_table),
	},
	.probe = ktz8864_probe,
	.remove = ktz8864_remove,
	.id_table = ktz8864_id,
};

static int __init ktz8864_iic_init(void)
{
	int ret;

	pr_info("ktz8864_iic_init\n");
	ret = i2c_add_driver(&ktz8864_driver);
	if (ret)
		pr_err("ktz8864_iic_init driver init failed!\n");
	return ret;
}

static void __exit ktz8864_iic_exit(void)
{
	pr_info("ktz8864_iic_exit\n");
	i2c_del_driver(&ktz8864_driver);
}

module_init(ktz8864_iic_init);
module_exit(ktz8864_iic_exit);

MODULE_DESCRIPTION("KTZ8864 backlight/bias driver (EEBBK OEM reconstruct)");
MODULE_LICENSE("GPL v2");

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TPS65132 LCD bias PMIC I2C driver
 * Reconstructed from EEBBK SDM660 OEM kernel 4.4.153-perf
 * DT compatible: "ti,ti65132"
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define TPS65132_REG_VPOS	0x00
#define TPS65132_REG_VNEG	0x01
#define TPS65132_REG_DISP	0x02
#define TPS65132_REG_CTRL	0x03
#define TPS65132_REG_WPROT	0xFF

#define TPS65132_ID_EXPECT	0x12
#define TPS65132_VPOS_VAL	0x12
#define TPS65132_VNEG_VAL	0x12
#define TPS65132_DISP_VAL	0xAA
#define TPS65132_CTRL_VAL	0x57
#define TPS65132_WPROT_VAL	0x80

static struct i2c_client *tps65132_client;
static DEFINE_MUTEX(tps65132_lock);

static int tps65132_write(struct i2c_client *client, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};
	int ret, retry = 3;

	mutex_lock(&tps65132_lock);
	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1) {
			mutex_unlock(&tps65132_lock);
			return 0;
		}
	} while (--retry > 0);
	mutex_unlock(&tps65132_lock);

	pr_err("%s: hgc_i2c_transfer(write) error, ret = %d\n",
	       __func__, ret);
	return ret < 0 ? ret : -EIO;
}

static int tps65132_read(struct i2c_client *client, u8 reg, u8 *val)
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

	mutex_lock(&tps65132_lock);
	do {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2) {
			mutex_unlock(&tps65132_lock);
			return 0;
		}
	} while (--retry > 0);
	mutex_unlock(&tps65132_lock);

	pr_err("[IIC]: hgc_i2c_transfer(read) error, ret=%d!!\n", ret);
	return ret < 0 ? ret : -EIO;
}

static int tps65132_apply_config(struct i2c_client *client)
{
	int ret;

	ret = tps65132_write(client, TPS65132_REG_VPOS, TPS65132_VPOS_VAL);
	if (ret)
		return ret;
	ret = tps65132_write(client, TPS65132_REG_VNEG, TPS65132_VNEG_VAL);
	if (ret)
		return ret;
	ret = tps65132_write(client, TPS65132_REG_CTRL, TPS65132_CTRL_VAL);
	if (ret)
		return ret;
	ret = tps65132_write(client, TPS65132_REG_WPROT, TPS65132_WPROT_VAL);
	return ret;
}

/* Re-apply bias config (exported for panel power sequencing if needed) */
int bbk_tps65132_config(void)
{
	u8 v0 = 0, v1 = 0, v3 = 0, vff = 0;

	if (!tps65132_client)
		return -ENODEV;

	tps65132_apply_config(tps65132_client);
	tps65132_read(tps65132_client, TPS65132_REG_VPOS, &v0);
	tps65132_read(tps65132_client, TPS65132_REG_VNEG, &v1);
	tps65132_read(tps65132_client, TPS65132_REG_CTRL, &v3);
	tps65132_read(tps65132_client, TPS65132_REG_WPROT, &vff);
	pr_debug("hgc_i2c_read_test: 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		 v0, v1, v3, vff);
	return 0;
}
EXPORT_SYMBOL(bbk_tps65132_config);

static int tps65132_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	u8 chip_id = 0;
	u8 v0, v1, v2, v3, vff;
	int ret;

	pr_info("tps65132_iic_probe\n");
	pr_info("hgc: info==>name = %s addr = 0x%x\n",
		client->name, client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("hgc I2C not supported\n");
		return -ENODEV;
	}

	tps65132_client = client;

	ret = tps65132_read(client, TPS65132_REG_VPOS, &chip_id);
	if (ret)
		pr_warn("tps65132: initial read failed, forcing config\n");

	if (chip_id != TPS65132_ID_EXPECT) {
		tps65132_write(client, TPS65132_REG_VPOS, TPS65132_VPOS_VAL);
		tps65132_write(client, TPS65132_REG_VNEG, TPS65132_VNEG_VAL);
		tps65132_write(client, TPS65132_REG_DISP, TPS65132_DISP_VAL);
		tps65132_write(client, TPS65132_REG_CTRL, TPS65132_CTRL_VAL);
		tps65132_write(client, TPS65132_REG_WPROT, TPS65132_WPROT_VAL);
	}

	tps65132_read(client, TPS65132_REG_VPOS, &v0);
	tps65132_read(client, TPS65132_REG_VNEG, &v1);
	tps65132_read(client, TPS65132_REG_DISP, &v2);
	tps65132_read(client, TPS65132_REG_CTRL, &v3);
	tps65132_read(client, TPS65132_REG_WPROT, &vff);
	pr_info("hgc_i2c_read_test: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		v0, v1, v2, v3, vff);

	return 0;
}

static int tps65132_remove(struct i2c_client *client)
{
	pr_info("tps65132_remove\n");
	if (tps65132_client == client)
		tps65132_client = NULL;
	return 0;
}

static const struct i2c_device_id tps65132_id[] = {
	{ "tps65132", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tps65132_id);

static const struct of_device_id tps65132_match_table[] = {
	{ .compatible = "ti,ti65132" },
	{}
};
MODULE_DEVICE_TABLE(of, tps65132_match_table);

static struct i2c_driver tps65132_driver = {
	.driver = {
		.name = "tps65132",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tps65132_match_table),
	},
	.probe = tps65132_probe,
	.remove = tps65132_remove,
	.id_table = tps65132_id,
};

static int __init tps65132_iic_init(void)
{
	int ret;

	pr_info("tps65132_iic_init\n");
	ret = i2c_add_driver(&tps65132_driver);
	if (ret)
		pr_err("tps65132_iic_init driver init failed!\n");
	return ret;
}

static void __exit tps65132_iic_exit(void)
{
	pr_info("tps65132_iic_exit\n");
	i2c_del_driver(&tps65132_driver);
}

module_init(tps65132_iic_init);
module_exit(tps65132_iic_exit);

MODULE_DESCRIPTION("TPS65132 LCD bias PMIC driver (EEBBK OEM reconstruct)");
MODULE_LICENSE("GPL v2");

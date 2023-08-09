// SPDX-License-Identifier: GPL-2.0
#include <linux/ctype.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/*
 * Adtran devices usually come with a main MAC address ending on 0 and
 * hence may have up to 16 MAC addresses per device.
 * The main MAC address is stored as variable MFG_MAC in ASCII format.
 */
static int adtran_mac_address_pp(void *priv, const char *id, int index,
				  unsigned int offset, void *buf,
				  size_t bytes)
{
	char *macaddr = buf;
	unsigned char digit;

	if (bytes != 17)
		return -EINVAL;

	if (macaddr[16] >= '0' && macaddr[16] <= '9')
		digit = macaddr[16] - '0';
	else if (macaddr[16] >= 'a' && macaddr[16] <= 'f')
		digit = macaddr[16] - 'a' + 10;
	else if (macaddr[16] >= 'A' && macaddr[16] <= 'F')
		digit = macaddr[16] - 'A' + 10;
	else
		return -EINVAL;

	if (index < 0 || index > (0xf - digit))
		return -EINVAL;

	digit += index;

	if (digit < 10)
		macaddr[16] = digit + '0';
	else
		macaddr[16] = digit - 10 + 'a';

	return 0;
}

static int adtran_add_cells(struct device *dev, struct nvmem_device *nvmem,
			     struct nvmem_layout *layout)
{
	struct nvmem_cell_info info;
	struct device_node *layout_np;
	char mfginfo[1024], *c, *t, *p;
	int ret = -EINVAL;

	ret = nvmem_device_read(nvmem, 0, sizeof(mfginfo), mfginfo);
	if (ret < 0)
		return ret;
	else if (ret != sizeof(mfginfo))
		return -EIO;

	layout_np = of_nvmem_layout_get_container(nvmem);
	if (!layout_np)
		return -ENOENT;

	c = mfginfo;
	while (*c != 0xff) {
		memset(&info, 0, sizeof(info));
		if (*c == '#')
			goto nextline;

		t = strchr(c, '=');
		if (!t)
			goto nextline;

		*t = '\0';
		++t;
		info.offset = t - mfginfo;
		if (!strcmp(c, "MFG_MAC"))
			info.read_post_process = adtran_mac_address_pp;

		/* process variable name: convert to lower-case, '_' -> '-' */
		p = c;
		do {
			*p = tolower(*p);
			if (*p == '_')
				*p = '-';
		} while (*++p);
		info.name = c;
		c = strchr(t, 0xa); /* find newline */
		if (!c)
			break;

		info.bytes = c - t;
		info.np = of_get_child_by_name(layout_np, info.name);
		ret = nvmem_add_one_cell(nvmem, &info);
		if (ret)
			break;

		++c;
		continue;

nextline:
		c = strchr(c, 0xa); /* find newline */
		if (!c)
			break;
		++c;
	}

	of_node_put(layout_np);

	return ret;
}

static int adtran_probe(struct platform_device *pdev)
{
	struct nvmem_layout *layout;

	layout = devm_kzalloc(&pdev->dev, sizeof(*layout), GFP_KERNEL);
	if (!layout)
		return -ENOMEM;

	layout->add_cells = adtran_add_cells;
	layout->dev = &pdev->dev;

	platform_set_drvdata(pdev, layout);

	return nvmem_layout_register(layout);
}

static int adtran_remove(struct platform_device *pdev)
{
	struct nvmem_layout *layout = platform_get_drvdata(pdev);

	nvmem_layout_unregister(layout);

	return 0;
}

static const struct of_device_id adtran_of_match_table[] = {
	{ .compatible = "adtran,mfginfo" },
	{},
};
MODULE_DEVICE_TABLE(of, adtran_of_match_table);

static struct platform_driver adtran_layout = {
	.driver = {
		.name = "adtran-layout",
		.of_match_table = adtran_of_match_table,
	},
	.probe = adtran_probe,
	.remove = adtran_remove,
};
module_platform_driver(adtran_layout);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_DESCRIPTION("NVMEM layout driver for Adtran mfginfo");

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>       /* everything... */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */

#include <linux/mm.h>
#include <linux/mm_types.h>

#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>

#include <linux/cdev.h>
#include <linux/device.h>

#define INCLUSIVE_CACHE_NAME "inclusive-cache"

// Drivers for SiFive InclusiveCache control port

struct inclusive_cache {
	struct cdev cdev;
	struct resource regs;
};

static int inclusive_cache_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct inclusive_cache *cache = container_of(
			cdev, struct inclusive_cache, cdev);

	file->private_data = cache;

	return 0;
}

static int inclusive_cache_mmap(
		struct file *file, struct vm_area_struct *vma)
{
	struct inclusive_cache *cache = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long max_size = cache->regs.end - cache->regs.start + 1;
	unsigned long pfn;

	if (max_size < PAGE_SIZE)
		max_size = PAGE_SIZE;

	if ((vma->vm_pgoff << PAGE_SHIFT) + size > max_size)
		return -EINVAL;

	pfn = (cache->regs.start >> PAGE_SHIFT) + vma->vm_pgoff;

	return io_remap_pfn_range(
			vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static struct file_operations inclusive_cache_fops  = {
	.owner = THIS_MODULE,
	.open  = inclusive_cache_open,
	.mmap  = inclusive_cache_mmap
};

static int inclusive_cache_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct inclusive_cache *cache;
	int result;
	dev_t devno;

	if (!dev->of_node)
		return -ENODEV;

	cache = devm_kmalloc(dev, sizeof(struct inclusive_cache), GFP_KERNEL);
	if (cache == NULL)
		return -ENOMEM;

	result = of_address_to_resource(dev->of_node, 0, &cache->regs);
	if (result) {
		dev_err(dev, "missng \"reg\" property\n");
		return result;
	}

	result = alloc_chrdev_region(&devno, 0, 1, INCLUSIVE_CACHE_NAME);
	if (result < 0) {
		dev_err(dev, "Can't get major number\n");
		return result;
	}

	cdev_init(&cache->cdev, &inclusive_cache_fops);
	cache->cdev.owner = THIS_MODULE;
	cache->cdev.ops = &inclusive_cache_fops;

	result = cdev_add(&cache->cdev, devno, 1);
	if (result < 0) {
		dev_err(dev, "could not add cdev\n");
		return result;
	}

	platform_set_drvdata(pdev, cache);

	dev_info(dev, "Inclusive Cache w/ major number %d\n", MAJOR(devno));

	return 0;
}

static int inclusive_cache_remove(struct platform_device *pdev)
{
	struct inclusive_cache *cache;

	cache = platform_get_drvdata(pdev);
	cdev_del(&cache->cdev);
	unregister_chrdev_region(cache->cdev.dev, 1);

	return 0;
}

static struct of_device_id inclusive_cache_of_match[] = {
	{ .compatible = "sifive,inclusivecache0" },
	{}
};

static struct platform_driver inclusive_cache_driver = {
	.driver = {
		.name = INCLUSIVE_CACHE_NAME,
		.of_match_table = inclusive_cache_of_match,
		.suppress_bind_attrs = true
	},
	.probe = inclusive_cache_probe,
	.remove = inclusive_cache_remove
};

builtin_platform_driver(inclusive_cache_driver);

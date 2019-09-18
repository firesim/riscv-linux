#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/io.h>

#include <linux/atomic.h>
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

#define DRAM_CACHE_NAME "dram-cache"
#define DRAM_CACHE_LOGADDR_BITS 37
#define EXTENT_SHIFT 30
#define EXTENT_BYTES (1L << EXTENT_SHIFT)
#define EXTENT_TABLE_ENTRIES (1L << (DRAM_CACHE_LOGADDR_BITS - EXTENT_SHIFT))
#define PAGES_PER_EXTENT (1L << (EXTENT_SHIFT - PAGE_SHIFT))

struct dram_cache {
	struct cdev ctrl_cdev;
	struct cdev exttab_cdev;
	struct cdev memory_cdev;
	struct resource ctrl_regs;
	struct resource exttab_regs;
	struct resource memory_regs;
};

//static void dram_cache_vma_open(struct vm_area_struct *vma)
//{
//}
//
//static void dram_cache_vma_close(struct vm_area_struct *vma)
//{
//}
//
//static int dram_cache_fault(struct vm_fault *vmf)
//{
//	unsigned long pfn;
//	struct page *page;
//	struct dram_cache *cache;
//
//	cache = vmf->vma->vm_file->private_data;
//	pfn = (cache->memory_regs.start >> PAGE_SHIFT) + vmf->pgoff;
//
//	printk(KERN_INFO "Fault on page %lx (pfn: %lx)\n",
//			vmf->address, pfn);
//
//	if (!pfn_valid(pfn))
//		return VM_FAULT_SIGBUS;
//
//	page = pfn_to_page(pfn);
//	get_page(page);
//	vmf->page = page;
//
//	return 0;
//}
//
//static struct vm_operations_struct dram_cache_vm_ops = {
//	.open = dram_cache_vma_open,
//	.close = dram_cache_vma_close,
//	.fault = dram_cache_fault
//};

static int dram_cache_exttab_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct dram_cache *cache = container_of(
			cdev, struct dram_cache, exttab_cdev);

	file->private_data = cache;

	return 0;
}

static int dram_cache_memory_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct dram_cache *cache = container_of(
			cdev, struct dram_cache, memory_cdev);

	file->private_data = cache;

	return 0;
}

static int dram_cache_ctrl_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct dram_cache *cache = container_of(
			cdev, struct dram_cache, ctrl_cdev);

	file->private_data = cache;

	return 0;
}

static int dram_cache_exttab_mmap(
		struct file *file, struct vm_area_struct *vma)
{
	struct dram_cache *cache = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long max_size = cache->exttab_regs.end - cache->exttab_regs.start + 1;
	unsigned long pfn;

	if (max_size < PAGE_SIZE)
		max_size = PAGE_SIZE;

	if ((vma->vm_pgoff << PAGE_SHIFT) + size > max_size)
		return -EINVAL;

	pfn = (cache->exttab_regs.start >> PAGE_SHIFT) + vma->vm_pgoff;

	return io_remap_pfn_range(
			vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static int dram_cache_memory_mmap(
		struct file *file, struct vm_area_struct *vma)
{
	struct dram_cache *cache = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long max_size = cache->memory_regs.end - cache->memory_regs.start + 1;
	unsigned long pfn;

	if ((vma->vm_pgoff << PAGE_SHIFT) + size > max_size)
		return -EINVAL;

	pfn = (cache->memory_regs.start >> PAGE_SHIFT) + vma->vm_pgoff;

	return io_remap_pfn_range(
			vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static int dram_cache_ctrl_mmap(
		struct file *file, struct vm_area_struct *vma)
{
	struct dram_cache *cache = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long max_size = cache->ctrl_regs.end - cache->ctrl_regs.start + 1;
	unsigned long pfn;

	if (max_size < PAGE_SIZE)
		max_size = PAGE_SIZE;

	if ((vma->vm_pgoff << PAGE_SHIFT) + size > max_size)
		return -EINVAL;

	pfn = (cache->ctrl_regs.start >> PAGE_SHIFT) + vma->vm_pgoff;

	return io_remap_pfn_range(
			vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static struct file_operations dram_cache_exttab_fops  = {
	.owner = THIS_MODULE,
	.open  = dram_cache_exttab_open,
	.mmap  = dram_cache_exttab_mmap
};

static struct file_operations dram_cache_memory_fops = {
	.owner = THIS_MODULE,
	.open  = dram_cache_memory_open,
	.mmap  = dram_cache_memory_mmap
};

static struct file_operations dram_cache_ctrl_fops = {
	.owner = THIS_MODULE,
	.open  = dram_cache_ctrl_open,
	.mmap  = dram_cache_ctrl_mmap
};

static int dram_cache_find_addr(
		struct device *dev, const char *compat, struct resource *regs)
{
	struct device_node *node;
	int result;

	node = of_find_compatible_node(dev->of_node, NULL, compat);
	if (node == NULL) {
		dev_err(dev, "Couldn't find node %s\n", compat);
		return -EINVAL;
	}

	result = of_address_to_resource(node, 0, regs);
	if (result < 0) {
		dev_err(dev, "Node %s has no \"reg\" property\n", compat);
		return result;
	}

	of_node_put(node);

	return 0;
}

static inline int dram_cache_add_cdev(
		struct cdev *cdev, struct file_operations *fops, dev_t devno)
{
	cdev_init(cdev, fops);
	cdev->owner = THIS_MODULE;
	cdev->ops = fops;

	return cdev_add(cdev, devno, 1);
}

static int dram_cache_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dram_cache *cache;
	int result;
	dev_t devno;

	if (!dev->of_node)
		return -ENODEV;

	cache = devm_kmalloc(dev, sizeof(struct dram_cache), GFP_KERNEL);
	if (cache == NULL)
		return -ENOMEM;

	result = dram_cache_find_addr(
			dev, "ucbbar,dram-cache-ctrl", &cache->ctrl_regs);
	if (result < 0)
		return result;

	result = dram_cache_find_addr(
			dev, "ucbbar,dram-cache-ext-tab", &cache->exttab_regs);
	if (result < 0)
		return result;

	result = dram_cache_find_addr(
			dev, "ucbbar,dram-cache-mem", &cache->memory_regs);
	if (result < 0)
		return result;

	result = alloc_chrdev_region(&devno, 0, 2, DRAM_CACHE_NAME);
	if (result < 0) {
		dev_err(dev, "Can't get major number\n");
		return result;
	}

	result = dram_cache_add_cdev(
		&cache->exttab_cdev, &dram_cache_exttab_fops, devno);
	if (result < 0) {
		dev_err(dev, "dram-cache: can't add ext-tab cdev\n");
		return result;
	}

	result = dram_cache_add_cdev(
		&cache->memory_cdev, &dram_cache_memory_fops, devno + 1);
	if (result < 0) {
		dev_err(dev, "dram-cache: can't add memory cdev\n");
		return result;
	}

	result = dram_cache_add_cdev(
		&cache->ctrl_cdev, &dram_cache_ctrl_fops, devno + 2);
	if (result < 0) {
		dev_err(dev, "dram-cache: can't add ctrl cdev\n");
		return result;
	}

	platform_set_drvdata(pdev, cache);

	dev_info(dev, "DRAM Cache w/ major number %d\n", MAJOR(devno));

	return 0;
}

static int dram_cache_remove(struct platform_device *pdev)
{
	struct dram_cache *cache;

	cache = platform_get_drvdata(pdev);
	cdev_del(&cache->exttab_cdev);
	cdev_del(&cache->memory_cdev);
	unregister_chrdev_region(cache->exttab_cdev.dev, 2);

	return 0;
}

static struct of_device_id dram_cache_of_match[] = {
	{ .compatible = "ucbbar,dram-cache" },
	{}
};

static struct platform_driver dram_cache_driver = {
	.driver = {
		.name = DRAM_CACHE_NAME,
		.of_match_table = dram_cache_of_match,
		.suppress_bind_attrs = true
	},
	.probe = dram_cache_probe,
	.remove = dram_cache_remove
};

builtin_platform_driver(dram_cache_driver);

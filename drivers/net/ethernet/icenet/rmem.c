#include <linux/errno.h>
#include <linux/types.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>

#include <linux/cdev.h>
#include <linux/device.h>

#define RMEM_NAME "remote-memory"
#define RMEM_IOCTRANS 0

struct rmem_info {
	struct device *dev;
	struct resource regs;
	int major;
	dev_t devno;
	struct cdev cdev;
};

static int rmem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	struct rmem_info *info;
	struct cdev *cdev = filp->f_inode->i_cdev;
	unsigned long pfn;

	if (vma->vm_pgoff != 0 || size > PAGE_SIZE)
		return -EINVAL;

	info = container_of(cdev, struct rmem_info, cdev);
	pfn = info->regs.start >> PAGE_SHIFT;

	return io_remap_pfn_range(
		vma, vma->vm_start, pfn, PAGE_SIZE, vma->vm_page_prot);
}

static int translate_user_page(struct rmem_info *info, unsigned long vaddr)
{
	int ret;
	struct page *page;

	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(vaddr, 1, 0, &page, NULL);
	up_read(&current->mm->mmap_sem);

	if (ret < 0)
		return ret;

	return page_to_pfn(page);
}

static long rmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct cdev *cdev = filp->f_inode->i_cdev;
	struct rmem_info *info = container_of(cdev, struct rmem_info, cdev);

	switch (cmd) {
	case RMEM_IOCTRANS:
		return translate_user_page(info, arg);
	}

	return -EINVAL;
}

static struct file_operations rmem_fops = {
	.owner   = THIS_MODULE,
	.open    = simple_open,
	//.release = simple_release,
	.mmap    = rmem_mmap,
	.unlocked_ioctl = rmem_ioctl
};

static int rmem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rmem_info *info;
	int err;

	if (!dev->of_node)
		return -ENODEV;

	info = devm_kmalloc(dev, sizeof(struct rmem_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, info);
	info->dev = dev;

	err = of_address_to_resource(dev->of_node, 0, &info->regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	err = alloc_chrdev_region(&info->devno, 0, 1, RMEM_NAME);
	if (err) {
		printk(KERN_ERR "Remote Mem: could not allocate character device region\n");
		return err;
	}

	printk(KERN_INFO "Remote Mem: allocated device with major number %d\n",
			MAJOR(info->devno));

	cdev_init(&info->cdev, &rmem_fops);
	info->cdev.owner = THIS_MODULE;
	info->cdev.ops = &rmem_fops;
	err = cdev_add(&info->cdev, info->devno, 1);

	if (err) {
		printk(KERN_ERR "Error adding remote mem character device\n");
		return err;
	}

	return 0;
}

static int rmem_remove(struct platform_device *pdev)
{
	struct rmem_info *info;

	info = platform_get_drvdata(pdev);
	cdev_del(&info->cdev);
	unregister_chrdev_region(info->devno, 1);

	return 0;
}

static struct of_device_id rmem_of_match[] = {
	{ .compatible = "ucbbar,remote-mem-client" },
	{}
};

static struct platform_driver rmem_driver = {
	.driver = {
		.name = RMEM_NAME,
		.of_match_table = rmem_of_match,
		.suppress_bind_attrs = true
	},
	.probe = rmem_probe,
	.remove = rmem_remove
};

builtin_platform_driver(rmem_driver);

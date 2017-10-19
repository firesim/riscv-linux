#include <linux/errno.h>
#include <linux/types.h>

#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/fs.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>

#include <linux/of_device.h>
//#include <linux/devfs_fs_kernel.h>
#include <linux/kernel.h>

#include <linux/cdev.h>

#include <asm/io.h>
#include <asm/page.h>

#define FSIM_DEVICE_NAME	"fsim-manager"
typedef uint32_t	fsim_request;
typedef uint32_t	fsim_response;

#define FSIM_MANAGER_REQUEST_OFFSET		0
#define FSIM_MANAGER_RESPONSE_OFFSET		4

static int major=0; /* major number we get from the kernel */

static struct class *fsim_manager_class;

struct fsim_manager_device {
	struct device *dev;
	void __iomem *iomem;
	struct cdev c_dev;
	spinlock_t lock;	
};

static int fsim_open(struct inode *inode, struct file *filp) {
	struct fsim_manager_device* dev;
	struct cdev* cdev = inode->i_cdev;
	dev = container_of(cdev, struct fsim_manager_device, c_dev);

	if (!dev) {
		printk(KERN_INFO "fsim-manager cannot retrieve device\n");
	}
	filp->private_data = dev;
	
	return 0;
}

static int fsim_release(struct inode *inode, struct file *filp) {
	return 0;
}

static ssize_t fsim_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos) {
	struct fsim_manager_device* dev = file->private_data;
	int ret;
	u32 data;
	u32 __user *tmp = (u32 __user *) buf;

	
	if (count != 4)
	{
		return -EINVAL;
	}
	
	spin_lock(&dev->lock);

	if(!dev->dev) {
		ret = -ENODEV;
		goto out;
	}

	data = ioread32(dev->iomem + FSIM_MANAGER_RESPONSE_OFFSET);

	if (copy_to_user(tmp, &data, 4)) {
		ret = -EFAULT;
		goto out;
	}
	ret = 4;
	
out:
	spin_unlock(&dev->lock);
	return ret;
}

static ssize_t fsim_write(struct file* file, const char __user *buf, size_t count, loff_t * f_pos) {
	struct fsim_manager_device *dev = file->private_data;
	const u32 __user *tmp = (const u32 __user *)buf;
	u32 data;
	int ret;
	
	
	if (count != 4) {
		return -EINVAL;
	}
	
	spin_lock(&dev->lock);

	if(!dev->dev) {
		ret = -ENODEV;
		goto out;
	}

	if (copy_from_user(&data, tmp, 4)) {
		ret = -EFAULT;
		goto out;
	}
	
	iowrite32(data, dev->iomem + FSIM_MANAGER_REQUEST_OFFSET);
	
	ret = 4;
out:
	spin_unlock(&dev->lock);
	return ret;
}

static const struct file_operations fsim_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
	.read		= fsim_read,
	.write 		= fsim_write,
	.open 		= fsim_open,
	/* no implementation below */
	.release 	= fsim_release,
};


static int fsim_manager_parse_dt(struct fsim_manager_device *fsim) {	
	struct device *dev = fsim->dev;
	struct device_node *node = dev->of_node;
	struct resource regs;
	int err;

	err = of_address_to_resource(node, 0, &regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	fsim->iomem = devm_ioremap_resource(dev, &regs);
	if (IS_ERR(fsim->iomem)) {
		dev_err(dev, "could not remap io address %llx", regs.start);
		return PTR_ERR(fsim->iomem);
	}

	return 0;
}

static int fsim_manager_probe(struct platform_device *pdev) {
	/* implement me */
	struct device *dev = &pdev->dev;
	struct fsim_manager_device *fsim;
	int err, ret;
	dev_t devno;

	fsim_manager_class = class_create(THIS_MODULE, "fsim");

	if(!dev->of_node) {
		printk(KERN_INFO "fsim-manager: error");
		return -ENODEV;
	}

	fsim = devm_kzalloc(dev, sizeof(*fsim), GFP_KERNEL);
	fsim->dev = dev;
	dev_set_drvdata(dev, fsim);

	spin_lock_init(&fsim->lock);

	err = fsim_manager_parse_dt(fsim);
	if (err) {
		dev_err(dev, "fsim-manager: Parsing DeviceTree failed\n");
		return err;
	}
	
	//major = register_chrdev(0, FSIM_DEVICE_NAME, &fsim_fops);
	
	ret = alloc_chrdev_region(&devno, 0, 1, FSIM_DEVICE_NAME);
	major = MAJOR(devno);

	if ( ret < 0 ) {
		printk(KERN_WARNING "fsim-manager: could not get major number\n");
	}

	cdev_init(&fsim->c_dev, &fsim_fops);
	fsim->c_dev.owner = THIS_MODULE;
	fsim->c_dev.ops = &fsim_fops;
	err = cdev_add (&fsim->c_dev, devno, 1);
	if (err)
		printk(KERN_NOTICE "Error %d adding fsim-manager", err);

	dev = device_create(fsim_manager_class, NULL, devno, fsim, FSIM_DEVICE_NAME);
	if(IS_ERR(dev)){
		printk(KERN_ERR "fsim-manager device_create faild\n");
		return -ENODEV;
	}

	return 0;
}

static int fsim_manager_remove(struct platform_device *pdev) {
	/* implement me */
	//struct device *dev = &pdev->dev;
	//struct fsim_manager_device *fsim = dev_get_drvdata(dev);
	unregister_chrdev(major, FSIM_DEVICE_NAME);

	return 0;
}


static struct of_device_id fsim_manager_of_match[] = {
	{ .compatible = "ucbbar,fsim-manager" },
	{}
};

static struct platform_driver fsim_manager_driver = {
	.driver = {
		.name = FSIM_DEVICE_NAME,
		.of_match_table = fsim_manager_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = fsim_manager_probe,
	.remove = fsim_manager_remove,
};

builtin_platform_driver(fsim_manager_driver);

/*
 * Eidetic IOMEM Device Driver
 * Copyright (c) 2022, Eideticom
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Copyright (C) 2022 Eideitcom
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-p2pdma.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pfn_t.h>

#define PCI_VENDOR_EIDETICOM 0x1de5
#define PCI_DEVICE_IOMEM 0x1000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Maier <andrew.maier@eideticom.com>");
MODULE_DESCRIPTION("Driver for the Eideticom IOMEM PCIe device");

static int max_devices = 16;
module_param(max_devices, int, 0444);
MODULE_PARM_DESC(max_devices, "Maximum number of eid-iomem devices");

static struct class *eid_iomem_class;
static DEFINE_IDA(eid_iomem_ida);
static dev_t eid_iomem_devt;

static struct pci_device_id eid_iomem_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_EIDETICOM, PCI_DEVICE_IOMEM), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, eid_iomem_pci_id_table);

struct eid_iomem_dev {
	struct device dev;
	struct pci_dev *pdev;
	int id;
	struct cdev cdev;
};

static int eid_iomem_open(struct inode *inode, struct file *filp)
{
	struct eid_iomem_dev *p;

	p = container_of(inode->i_cdev, struct eid_iomem_dev, cdev);
	filp->private_data = p;
	pci_p2pdma_file_open(p->pdev, filp);

	return 0;
}

static int eid_iomem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct eid_iomem_dev *p = filp->private_data;

	return pci_mmap_p2pmem(p->pdev, vma);
}

static const struct file_operations eid_iomem_fops = {
	.owner = THIS_MODULE,
	.open = eid_iomem_open,
	.mmap = eid_iomem_mmap,
};

static void eid_iomem_release(struct device *dev)
{
	struct eid_iomem_dev *d = container_of(dev, struct eid_iomem_dev, dev);

	pci_dev_put(d->pdev);
	ida_simple_remove(&eid_iomem_ida, d->id);
	kfree(d);
}

static struct eid_iomem_dev *eid_iomem_create(struct pci_dev *pdev)
{
	struct eid_iomem_dev *d;
	int err;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return ERR_PTR(-ENOMEM);

	d->pdev = pci_dev_get(pdev);

	device_initialize(&d->dev);
	d->dev.class = eid_iomem_class;
	d->dev.parent = &pdev->dev;
	d->dev.release = eid_iomem_release;

	d->id = ida_simple_get(&eid_iomem_ida, 0, 0, GFP_KERNEL);
	if (d->id < 0) {
		err = d->id;
		goto out_free;
	}

	dev_set_name(&d->dev, "eid-iomem%d", d->id);
	d->dev.devt = MKDEV(MAJOR(eid_iomem_devt), d->id);

	cdev_init(&d->cdev, &eid_iomem_fops);
	d->cdev.owner = THIS_MODULE;

	err = cdev_device_add(&d->cdev, &d->dev);
	if (err)
		goto out_ida;

	dev_info(&d->dev, "registered");

	return d;

out_ida:
	ida_simple_remove(&eid_iomem_ida, d->id);
out_free:
	put_device(&d->dev);
	kfree(d);
	return ERR_PTR(err);
}

void eid_iomem_destroy(struct eid_iomem_dev *p)
{
	dev_info(&p->dev, "unregistered");
	cdev_device_del(&p->cdev, &p->dev);
	put_device(&p->dev);
}

static int eid_iomem_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct eid_iomem_dev *p;
	int err = 0;

	err = pcim_enable_device(pdev);
	if (err) {
		pci_err(pdev, "unable to enable device!\n");
		goto out;
	}

	err = pci_p2pdma_add_resource(pdev, 0, 0, 0);
	if (err) {
		dev_err(&pdev->dev, "unable to add p2p resource");
		goto out_disable_device;
	}

	pci_p2pmem_publish(pdev, true);

	p = eid_iomem_create(pdev);
	if (IS_ERR(p)) {
		err = PTR_ERR(p);
		goto out_disable_device;
	}

	pci_set_drvdata(pdev, p);

	return 0;

out_disable_device:
	pci_disable_device(pdev);
out:
	return err;
}

static void eid_iomem_pci_remove(struct pci_dev *pdev)
{
	struct eid_iomem_dev *p = pci_get_drvdata(pdev);

	eid_iomem_destroy(p);
}

static struct pci_driver eid_iomem_pci_driver = {
	.name = "eid_iomem_pci",
	.id_table = eid_iomem_pci_id_table,
	.probe = eid_iomem_pci_probe,
	.remove = eid_iomem_pci_remove,
};

static int __init eid_iomem_pci_init(void)
{
	int rc;

	eid_iomem_class = class_create(THIS_MODULE, "eid_iomem_device");
	if (IS_ERR(eid_iomem_class))
		return PTR_ERR(eid_iomem_class);

	rc = alloc_chrdev_region(&eid_iomem_devt, 0, max_devices, "eid-iomem");
	if (rc)
		goto err_class;

	rc = pci_register_driver(&eid_iomem_pci_driver);
	if (rc)
		goto err_chdev;

	pr_info(KBUILD_MODNAME ": module loaded\n");

	return 0;
err_chdev:
	unregister_chrdev_region(eid_iomem_devt, max_devices);
err_class:
	class_destroy(eid_iomem_class);
	return rc;
}

static void __exit eid_iomem_pci_cleanup(void)
{
	pci_unregister_driver(&eid_iomem_pci_driver);
	unregister_chrdev_region(eid_iomem_devt, max_devices);
	class_destroy(eid_iomem_class);
	pr_info(KBUILD_MODNAME ": module unloaded\n");
}

module_init(eid_iomem_pci_init);
module_exit(eid_iomem_pci_cleanup);

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/module.h>

MODULE_LICENSE("Dual BSD/GPL");

#define MODULE_NAME         "interleave_mem"

//=============================================================================

struct pattern {
    size_t len;
    uint8_t nodes[];
};

static ssize_t write(struct file* file, const char* buf, size_t len,
                        loff_t* offset) {
    if (len) {
        size_t struct_size = sizeof(size_t) + len;
        struct pattern* pattern = kmalloc(struct_size, GFP_KERNEL);
        if (!pattern) {
            printk(KERN_ERR "failed to kmalloc(%lu)\n", struct_size);
            return -ENOMEM;
        }
        pattern->len = len;
        if (copy_from_user(pattern->nodes, buf, len) != 0) {
            printk(KERN_ERR "failed to copy_from_user(pattern->nodes, %p, "
                            "%lu)\n", buf, len);
            kfree(pattern);
            return -EIO;
        }
        if (file->private_data) {
            kfree(file->private_data);
        }
        file->private_data = pattern;
    }
    else if (file->private_data) {
        kfree(file->private_data);
        file->private_data = NULL;
    }
    return len;
}

static vm_fault_t fault(struct vm_fault* vmf) {
    struct pattern* pattern = vmf->vma->vm_file->private_data;
    int node;
    struct page* page;
    if (pattern) {
        unsigned long pfn = vmf->address >> PAGE_SHIFT;
        node = pattern->nodes[pfn % pattern->len];
        if (node >= MAX_NUMNODES || !node_online(node)) {
            return VM_FAULT_SIGBUS;
        }
    }
    else {
        node = NUMA_NO_NODE;
    }
    page = alloc_pages_node(node, GFP_HIGHUSER_MOVABLE, 0);
    if (!page) {
        printk(KERN_ERR "failed to allocate a page on node %d\n", node);
        return VM_FAULT_OOM;
    }
    vmf->page = page;
    return 0;
}

static struct vm_operations_struct vma_ops = {
    .fault = fault,
};

static int mmap(struct file* file, struct vm_area_struct* vma) {
    if (!(vma->vm_flags & VM_SHARED)) {
        printk(KERN_ERR "%s only supports shared mapping\n", MODULE_NAME);
        return -EINVAL;
    }
    vma->vm_ops = &vma_ops;
    return 0;
}

static int release(struct inode* inode, struct file* file) {
    if (file->private_data) {
        kfree(file->private_data);
    }
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = write,
    .mmap = mmap,
    .release = release,
};

//=============================================================================

static dev_t number;
static struct cdev cdev;
static struct class* class;
static struct device* device;

static int init(void) {
    int ret;
    ret = alloc_chrdev_region(&number, 0, 1, MODULE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "failed to alloc_chrdev_region(&number, 0, 1, "
                        "'%s')\n", MODULE_NAME);
        return ret;
    }
    cdev_init(&cdev, &fops);
    ret = cdev_add(&cdev, number, 1);
    if (ret < 0) {
        printk(KERN_ERR "failed to cdev_add(&cdev, number, 1)\n");
        goto clean_1;
    }
    ret = -1;
    class = class_create(THIS_MODULE, MODULE_NAME);
    if (IS_ERR(class)) {
        printk(KERN_ERR "failed to class_create(THIS_MODULE, '%s')\n",
                            MODULE_NAME);
        goto clean_2;
    }
    device = device_create(class, NULL, number, NULL, MODULE_NAME);
    if (IS_ERR(device)) {
        printk(KERN_ERR "failed to device_create(class, NULL, number, NULL, "
                        "'%s')\n", MODULE_NAME);
        goto clean_3;
    }
    return 0;

clean_3:
    class_destroy(class);
clean_2:
    cdev_del(&cdev);
clean_1:
    unregister_chrdev_region(number, 1);
    return ret;
}

static void cleanup(void) {
    device_destroy(class, number);
    class_destroy(class);
    cdev_del(&cdev);
    unregister_chrdev_region(number, 1);
}

module_init(init);
module_exit(cleanup);
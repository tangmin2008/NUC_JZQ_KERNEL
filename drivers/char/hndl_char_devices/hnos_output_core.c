/*
 *  drivers/char/hndl_char_devices/hnos_rmc_core.c.
 *
 *  ReMote Control (RMC) interface.
 *
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include "hnos_generic.h"
#include "hnos_ioctl.h" 
#include "hnos_proc.h" 
#include "hnos_gpio.h" 
#include "hnos_output.h"

#define DEVICE_NAME	"output"

static struct class *class;
static  struct proc_dir_entry	*hndl_proc_dir = NULL;
static struct hndl_rmc_device *outputs[NR_OUTPUT_DEVICES];
static DEFINE_SPINLOCK(outputs_lock);
static int rmc_major =   0;
static int rmc_minor =   -1;

static inline int rmc_smcbus_read_all(struct smcbus_rmc_data *bus, u32 *reslt);
static inline int rmc_smcbus_write(struct smcbus_rmc_data *bus, 
        u32 bitmap, int is_set);

static void rmc_smcbus_refresh_timer(unsigned long data)
{
    struct hndl_rmc_device *dev = (struct hndl_rmc_device *)data;
    struct smcbus_refresh_data *refresh = dev->bus_refresh;
    u32 tmp = 0;
    int ret = 0;
    unsigned long flags; 

    if (!refresh) {
        return;
    }

    spin_lock_irqsave(&dev->lock, flags);

    refresh->refresh_cnt ++;

    ret = rmc_smcbus_read_all(dev->smcbus, &tmp);
    if (ret < 0) {
        goto out;
    }

    rmc_smcbus_write(dev->smcbus, tmp, 1);
    SMCBUS_CHIP_ENABLE();
out:
    spin_unlock_irqrestore(&dev->lock, flags);
    /* resubmit the timer again */
    refresh->timer.expires = jiffies + REFRESH_FREQ_TIMEOUT;
    add_timer(&refresh->timer);
    return;
}

int rmc_smcbus_refresh_start(struct hndl_rmc_device *dev)
{
    int ret = 0;
    struct smcbus_refresh_data *refresh = dev->bus_refresh;
    unsigned long flags; 

    if (!refresh) {
        return -1;
    }

    spin_lock_irqsave(&dev->lock, flags);
    if (refresh->is_timer_started) {
        printk("%s: refresh timer already started.\n", __FUNCTION__);
        ret = -1;
        goto out;
    }

    refresh->is_timer_started = 1;
    spin_unlock_irqrestore(&dev->lock, flags);

    /* submit the timer. */
    init_timer(&refresh->timer);

    refresh->timer.function = rmc_smcbus_refresh_timer;
    refresh->timer.data = (unsigned long)dev;
    refresh->timer.expires = jiffies + REFRESH_FREQ_TIMEOUT;

    add_timer(&refresh->timer);
    return 0;
out:
    spin_unlock_irqrestore(&dev->lock, flags);
    return ret;
}

int rmc_smcbus_refresh_stop(struct hndl_rmc_device *dev)
{
    int ret = 0;
    unsigned long flags; 
    struct smcbus_refresh_data *refresh = dev->bus_refresh;

    if (!refresh) {
        return -1;
    }

    spin_lock_irqsave(&dev->lock, flags);

    if (!refresh->is_timer_started) {
        printk("%s: refresh timer NOT started.\n", __FUNCTION__);
        ret = -1;
        goto out;
    }

    refresh->is_timer_started = 0;

    spin_unlock_irqrestore(&dev->lock, flags);

    del_timer_sync(&refresh->timer);
    return 0;

out:
    spin_unlock_irqrestore(&dev->lock, flags);
    return ret;
}

static int rmc_refresh_proc_read(char *buf, char **start, 
        off_t off, int count, 
        int *eof, void *data)
{
    int len = 0;
    struct hndl_rmc_device *dev = (struct hndl_rmc_device *)data;
    struct smcbus_refresh_data *refresh = dev->bus_refresh;
    if (!refresh) {
        return -EINVAL;
    }

    len += sprintf(buf, "cnt %ld\n", refresh->refresh_cnt);
    if (len < 0)
        return len;

    if (len <= off + count)
        *eof = 1;
    *start = buf + off;
    len -= off;
    if (len > count)
        len = count;
    if (len < 0)
        len = 0;

    return len;
}

static int rmc_refresh_proc_write(struct file *file, 
        const char __user * userbuf,
        unsigned long count, void *data)
{
    struct hndl_rmc_device *dev = (struct hndl_rmc_device *)data;
    char val[50] = {0};
    int ret = 0;

    if (!dev->bus_refresh) {
        return -EINVAL;
    }

    if (count >= 50){
        return -EINVAL;
    }

    if (copy_from_user(val, userbuf, count)) {
        return -EFAULT;
    }

    if (strncmp(val, "start", 5) == 0){
        ret = rmc_smcbus_refresh_start(dev);
    } else if (strncmp(val, "stop", 4) == 0) {
        ret = rmc_smcbus_refresh_stop(dev);
    }

    dprintk(KERN_INFO "%s: buf %s.\n", __FUNCTION__, val);

    if (0 == ret) {
        return count;
    }
    return ret;
}


static int rmc_smcbus_proc_read(char *buf, char **start,
        off_t off, int count,
        int *eof, void *data)
{
    int len = 0;
    struct smcbus_rmc_data *smcbus = (struct smcbus_rmc_data *)data;

    if (smcbus && smcbus->proc_read) {
        len = smcbus->proc_read(smcbus, buf);
    }

    if (len < 0)
        return len;

    if (len <= off + count)
        *eof = 1;
    *start = buf + off;
    len -= off;
    if (len > count)
        len = count;
    if (len < 0)
        len = 0;

    return len;
}

static int rmc_smcbus_proc_write(struct file *file, 
        const char __user * userbuf, 
        unsigned long count, void *data)
{
    struct smcbus_rmc_data *smcbus = (struct smcbus_rmc_data *)data;
    int ret;

    if (!smcbus || !smcbus->proc_write) {
        return -EINVAL;
    }

    ret = smcbus->proc_write(smcbus, userbuf, count);	
    if (ret == 0) {
        ret = count;
    }

    return ret;
}

static int rmc_smcbus_proc_create(struct hndl_rmc_device *output)
{
    struct proc_dir_entry *proc;
    struct proc_dir_entry *proc1;

    unsigned char name[18] = {0};
    sprintf(name, "smcbus-output%d", MINOR(output->cdev.dev));

    if (output->smcbus && hndl_proc_dir) {
        proc = create_proc_read_entry(name,
                S_IFREG | S_IRUGO | S_IWUSR,
                hndl_proc_dir,
                rmc_smcbus_proc_read,
                output->smcbus);
        if (!proc) {
            return -1;
        }

        if (proc) {
            proc->read_proc = (read_proc_t *) rmc_smcbus_proc_read;
            proc->write_proc = (write_proc_t *) rmc_smcbus_proc_write;
        }

    }

    memset(name, 0, sizeof(name));
    sprintf(name, "smcbus-refresh%d", MINOR(output->cdev.dev));

    if (output->smcbus && output->bus_refresh && hndl_proc_dir) {
        proc1 = create_proc_read_entry(name,
                S_IFREG | S_IRUGO | S_IWUSR,
                hndl_proc_dir,
                rmc_refresh_proc_read,
                output);
        if (!proc1) {
            return -1;
        }

        if (proc1) {
            proc1->read_proc = (read_proc_t *) rmc_refresh_proc_read;
            proc1->write_proc = (write_proc_t *) rmc_refresh_proc_write;
        }

        return 0;
    }

    return 0; /* Revisit */
}

static int rmc_gpio_proc_del(struct hndl_rmc_device *output)
{
    struct gpio_rmc_data *gpio = output->gpio;
    struct proc_item *item = gpio->items;
    int i = 0;

    for (i=0; i<gpio->size; i++) {
        item = &gpio->items[i];
        remove_proc_entry(item->name, hndl_proc_dir);
    }

    return 0;
}

static int  rmc_gpio_proc_create(struct hndl_rmc_device *output)
{
    struct gpio_rmc_data *gpio = output->gpio;
    struct proc_item *item = gpio->items;
    int ret = 0, i = 0;

    for (i=0; i<gpio->size; i++) {
        item = &gpio->items[i];
        hnos_gpio_cfg(item->pin, item->settings);
        ret += hnos_proc_entry_create(item);
    }
    return ret;
}

static  int rmc_smcbus_proc_del(struct hndl_rmc_device *output)
{
    unsigned char name[18] = {0};

    if (output->smcbus && hndl_proc_dir) {
        sprintf(name, "smcbus-output%d", MINOR(output->cdev.dev));
        remove_proc_entry(name, hndl_proc_dir);

        memset(name, 0, sizeof(name));
        sprintf(name, "smcbus-refresh%d", MINOR(output->cdev.dev));
        remove_proc_entry(name, hndl_proc_dir);
    }

    return 0;
}

int rmc_gpio_register(struct hndl_rmc_device *output, 
        struct gpio_rmc_data *gpio,
        u8 offset, u8 size)
{
    unsigned long flags; 

    spin_lock_irqsave(&output->lock, flags);
    output->gpio = gpio;
    output->gpio_offset = offset;
    output->gpio_end = size - 1;
    spin_unlock_irqrestore(&output->lock, flags);

    HNOS_DEBUG_INFO("GPIOs for Output Control registered, offset %d, end %d.\n",
            offset, size - 1);
    return 0;
}

int rmc_smcbus_register(struct hndl_rmc_device *output, 
        struct smcbus_rmc_data *bus,
        u8 offset, u8 size)
{
    u8 smcbus_off = offset; 
    u8 smcbus_end = offset + size - 1;
    struct smcbus_refresh_data *refresh = NULL;
    unsigned long flags; 

    spin_lock_irqsave(&output->lock, flags);

    if (0 == offset) {
        if (output->gpio) {
            smcbus_off = output->gpio_end + 1;
        } else {
            smcbus_off = 0;
        }

        smcbus_end = smcbus_off + size - 1;
    }
    if (smcbus_end >= OUTPUT_CHAN_MAX) {
        printk(KERN_WARNING "%s: so many smcbus channels:-(\n",
                __FUNCTION__);
        spin_unlock_irqrestore(&output->lock, flags);
        return -EFAULT;
    }

    output->smcbus = bus;
    output->smcbus_offset = smcbus_off;
    output->smcbus_end = smcbus_end;

    spin_unlock_irqrestore(&output->lock, flags);

    refresh = kmalloc(sizeof(struct smcbus_refresh_data), GFP_KERNEL);
    if (!refresh) {
        printk(KERN_ERR "%s: No memory available.\n", __FUNCTION__);
        return -ENOMEM;
    }	
    memset(refresh, 0, sizeof(struct smcbus_refresh_data));
    spin_lock_init(&refresh->lock);
    init_timer(&refresh->timer);

    output->bus_refresh = refresh;

    HNOS_DEBUG_INFO("SMCBUS for Output Control registered, offset %d, end %d.\n", 
            smcbus_off, smcbus_end);
    return 0;
}

int rmc_gpio_unregister(struct hndl_rmc_device *output, 
        struct gpio_rmc_data *gpio)
{
    unsigned long flags; 

    spin_lock_irqsave(&output->lock, flags);
    rmc_gpio_proc_del(output);

    output->gpio = NULL;
    output->gpio_offset = 0;
    output->gpio_end = 0;
    spin_unlock_irqrestore(&output->lock, flags);

    return 0;
}

int rmc_smcbus_unregister(struct hndl_rmc_device *output, 
        struct smcbus_rmc_data *bus)
{
    unsigned long flags; 

    if (output->bus_refresh) {
        rmc_smcbus_refresh_stop(output); /* Make sure the refresh timer had been stopped. */
        kfree(output->bus_refresh);
        output->bus_refresh = NULL;
    }

    spin_lock_irqsave(&output->lock, flags);
    rmc_smcbus_proc_del(output);

    output->smcbus = NULL;
    output->smcbus_offset = 0;
    output->smcbus_end = 0;
    spin_unlock_irqrestore(&output->lock, flags);

    return 0;
}

static int rmc_gpio_read_all(struct gpio_rmc_data *gpio, u32 *reslt)
{
    int i = 0, tmp = 0;

    if (!gpio) {
        return -EFAULT;
    }

    for (i=0; i<gpio->size; i++) {
        tmp = (at91_get_gpio_value(gpio->items[i].pin) ? 1 : 0);
        *reslt |= (tmp << i); 
    }

    return 0;

}

static inline int rmc_smcbus_read_all(struct smcbus_rmc_data *bus, u32 *reslt)
{
    if (bus && bus->read) {
        return bus->read(bus, reslt);
    } 

    *reslt = bus->smcbus_stat;
    return 0;
}

static int rmc_read_ops(struct hndl_rmc_device *output, u32 *reslt)
{
    int ret = 0;
    u32 gpio = 0;
    u32 smcbus = 0;
    unsigned long flags; 

    if (!output) {
        return -EFAULT;
    }

    spin_lock_irqsave(&output->lock, flags);

    if (output->gpio) {
        ret = rmc_gpio_read_all(output->gpio, &gpio);
        if (ret < 0) {
            goto fail;
        }
    }

    if (output->smcbus) {
        ret = rmc_smcbus_read_all(output->smcbus, &smcbus);
        if (ret < 0) {
            goto fail;
        }
    }

    *reslt = ( smcbus << output->smcbus_offset ) | gpio;

    dprintk("%s: reslt %x, smcbus %x, gpio %x, shift %d.\n", __FUNCTION__,
            *reslt, smcbus, gpio, output->smcbus_offset);
fail:
    spin_unlock_irqrestore(&output->lock, flags);
    return ret;
}

static int rmc_smcbus_stat_update(struct smcbus_rmc_data *bus,
        u32 bitmap, int is_set)
{
    u32 stat = bus->smcbus_stat;

    if (is_set) {
        stat |= bitmap;
    } else {
        stat &= ~bitmap;
    }

    bus->smcbus_stat = stat;
    return 0;
}

static int rmc_gpio_write(struct gpio_rmc_data *gpio,
        u32 bitmap, int is_set)
{
    int i = 0;
    int pin = 0;
    int value = (is_set ? 1 : 0);
    int chan_max = gpio->size;
    int ret = 0;

    struct proc_item *items = gpio->items;

    for (i=0; i<chan_max; i++) {
        pin = items[i].pin;
        if (bitmap & (0x1 << i)) {
            ret = at91_set_gpio_value(pin, value);
            dprintk("%s, pin %d, value %d.\n", __FUNCTION__, pin, value);
            if (ret) {
                return ret;
            }
        }
    }

    return ret;
}

static inline int rmc_smcbus_write(struct smcbus_rmc_data *bus, 
        u32 bitmap, int is_set)
{
    if (bus && bus->write) {
        return bus->write(bus, bitmap, is_set);
    }
    printk("%s: No smcbus.\n", __FUNCTION__);
    return -EFAULT;
}


static int rmc_write_ops(struct hndl_rmc_device *dev, u32 bitmap, int is_set)
{
    int ret = 0;
    unsigned long flags; 
    unsigned long gpios_mask = ( 1 << (dev->gpio_end + 1) ) - 1;

    spin_lock_irqsave(&dev->lock, flags);

    if (dev->gpio && (bitmap & gpios_mask)) {
        ret = rmc_gpio_write(dev->gpio, 
                (bitmap >> dev->gpio_offset), is_set);
        if (ret) {
            printk("%s: rmc_gpio_read error, ret %d, bitmap %8x.\n", 
                    __FUNCTION__, ret, bitmap);
        }
    }

    if (dev->smcbus && (bitmap >> dev->smcbus_offset)) 
    	{
        ret = rmc_smcbus_write(dev->smcbus,
                (bitmap >> dev->smcbus_offset), is_set);
        if (ret) {
            printk("%s: rmc_smcbus_write error, ret %d, bitmap %8x.\n",
                    __FUNCTION__, ret, bitmap);
        }
        rmc_smcbus_stat_update(dev->smcbus, 
                (bitmap >> dev->smcbus_offset), is_set);
    }

    spin_unlock_irqrestore(&dev->lock, flags);
    return ret;
}

static int rmc_open(struct inode *inode, struct file *filp)
{
    static struct hndl_rmc_device *dev;

    dev = container_of(inode->i_cdev, struct hndl_rmc_device, cdev);
    filp->private_data = dev; /* for other methods */

    if (test_and_set_bit(0, &dev->is_open) != 0) {
        return -EBUSY;       
    }

    return 0; /* success. */
}

static int rmc_release(struct inode *inode, struct file *filp)
{
    struct hndl_rmc_device *dev = filp->private_data; 

    if (test_and_clear_bit(0, &dev->is_open) == 0) { /* release lock, and check... */
        return -EINVAL; /* already released: error */
    }

    return 0;
}

static ssize_t rmc_read(struct file *filp, char __user *buf, 
        size_t count, loff_t *f_pos)
{
    //	struct hndl_rmc_device *dev = filp->private_data; 
    short reslt = 0;
    /* To be implemented */

    return sizeof(reslt);
}

/*The ioctl() implementation */
static int rmc_ioctl (struct inode *inode, struct file *filp,
        unsigned int cmd, unsigned long arg)
{
    int err = 0; 
    u32 stat = 0;
    unsigned char pin_number ;
    struct hndl_rmc_device *dev = filp->private_data; 

    /* FIX ME. */
    int chan_max = (dev->smcbus_end ? dev->smcbus_end : dev->gpio_end); 

    /* don't even decode wrong cmds: better returning  ENOTTY than EFAULT */
    if (_IOC_TYPE(cmd) != HNDL_AT91_IOC_MAGIC) {
        return -ENOTTY;
    }
    if (_IOC_NR(cmd) > HNDL_AT91_IOC_MAXNR ) {
        return -ENOTTY;
    }

    dprintk("%s: cmd %02x ", __FUNCTION__, cmd);
    
     switch (cmd) {
    	
    	case IOC_CTRL_PIN_R:

    	 	get_user(pin_number,(unsigned char __user *) arg);
    	    	  	   
    	  if (put_user((unsigned char)(at91_get_gpio_value((unsigned )(PIN_BASE+pin_number))), (unsigned char __user *) arg)) {
              
                return -EFAULT;
            }
          
    	  	return 0;
    	 case IOC_CTRL_PIN_S:
    	  	   get_user(pin_number,(unsigned char __user *) arg);
    	  	   at91_set_gpio_value((unsigned )(PIN_BASE+pin_number),1);
    	  	   at91_set_gpio_output((unsigned )(PIN_BASE+pin_number), 1);
    	  	   if (put_user((unsigned char)(at91_get_gpio_value((unsigned)(PIN_BASE+pin_number))), (unsigned char __user *) arg)) {
                return -EFAULT;
            }
    	  	return 0;
    	 case IOC_CTRL_PIN_C:
             get_user(pin_number,(unsigned char __user *) arg);
    	  	  at91_set_gpio_value((unsigned )(PIN_BASE+pin_number),0);
    	  	  at91_set_gpio_output((unsigned )(PIN_BASE+pin_number), 0);
    	  	   if (put_user((unsigned char)(at91_get_gpio_value((unsigned)(PIN_BASE+pin_number))), (unsigned char __user *) arg)) {
                return -EFAULT;
           }
    	  	return 0;
        case IOC_OUTPUT_CHAN_MAX:	/* The Maximum channels of output signal.*/
            //	dprintk("IOC_OUTPUT_CHAN_MAX.\n");
            return (chan_max + 1);

        case IOC_OUTPUT_CTRL_SET:	/* Set the the spcified channel according to the bitmap.*/
            dprintk("IOC_OUTPUT_CTRL_SET, bitmap %8x\n", arg);
            err = rmc_write_ops(dev, arg, 1);
            return err;

        case IOC_OUTPUT_CTRL_CLEAR:	/* Clear the the spcified channel according to the bitmap.*/
            dprintk("IOC_OUTPUT_CTRL_CLEAR, bitmap %8x\n", arg);
            err = rmc_write_ops(dev, arg, 0);
            return err;

        case IOC_OUTPUT_ALL_CHAN:	/* Get status of all the channels. */
            //	dprintk("IOC_OUTPUT_ALL_CHAN.\n");
            err = rmc_read_ops(dev, &stat);
            if (err) {
                return -EIO;
            }

            if (put_user(stat, (unsigned long __user *) arg)) {
                return -EFAULT;
            }

            return 0;

        default:  
            return -ENOTTY;
    }

    return 0;
}



struct file_operations hndl_rmc_fops = {
    .owner =    THIS_MODULE,
    .open = rmc_open,
    .release = rmc_release,
    .read = rmc_read,
    .ioctl = rmc_ioctl,
};

struct hndl_rmc_device *rmc_device_alloc(void)
{
    struct hndl_rmc_device *dev;

    dev = kmalloc(sizeof(struct hndl_rmc_device), GFP_KERNEL);
    if (!dev) {
        printk(KERN_ERR "%s: No memory available.\n", __FUNCTION__);
        return NULL;
    }	

    memset(dev, 0, sizeof(struct hndl_rmc_device));
    spin_lock_init(&dev->lock);	

    return dev;
}

void rmc_device_free(struct hndl_rmc_device *dev)
{
    kfree(dev);
    dev = NULL;
    return;
}

int rmc_device_register(struct hndl_rmc_device *dev)
{
    dev_t devno;
    int ret = 0;
    unsigned char name[10] = {0};
    unsigned long flags;

    spin_lock_irqsave(&outputs_lock, flags);
    if ((++rmc_minor) >= NR_OUTPUT_DEVICES) {		
        spin_unlock_irqrestore(&outputs_lock, flags);		
        return -1;
    }
    outputs[rmc_minor] = dev;
    spin_unlock_irqrestore(&outputs_lock, flags);

    devno = MKDEV(rmc_major, rmc_minor);
    cdev_init(&dev->cdev, &hndl_rmc_fops);
    dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev->cdev, devno, 1);
    if (ret) {   
        printk(KERN_NOTICE "Error %d adding major=%d \n", ret, MAJOR(devno));
        return ret;
    }{
    	   HNOS_DEBUG_INFO(" Add  rmc_device[cdev_add] major=%d,MINOR=%d success.\n", rmc_major,rmc_minor);	
    }

    sprintf(name, "output%d", rmc_minor);
    class_device_create(class, NULL, devno, NULL, name);

    if (dev->smcbus) {
        ret = rmc_smcbus_proc_create(dev);
        if (ret) {   
            printk(KERN_NOTICE "Error %d adding major=%d \n", ret, MAJOR(devno));
            return ret;
        }
    }

    if (dev->gpio) {
        ret = rmc_gpio_proc_create(dev);
        if (ret) {   
            printk(KERN_NOTICE "Error %d adding major=%d \n", ret, MAJOR(devno));
            return ret;
        }
    }

    HNOS_DEBUG_INFO("Output device %s registered.\n", name);	
    return ret;
}

int rmc_device_unregister(struct hndl_rmc_device *dev)
{
    dev_t devno = dev->cdev.dev;
    unsigned long flags;

    cdev_del(&dev->cdev);
    class_device_destroy(class, devno);

    spin_lock_irqsave(&outputs_lock, flags);
    if ((MINOR(devno)) >= NR_OUTPUT_DEVICES) {		
        spin_unlock_irqrestore(&outputs_lock, flags);		
        return -1;
    }
    outputs[MINOR(devno)] = NULL;
    spin_unlock_irqrestore(&outputs_lock, flags);		

    HNOS_DEBUG_INFO("Output device output%d unregistered.\n", MINOR(devno));	
    return 0;
}

static void  rmc_module_cleanup(void)
{
    dev_t devno = MKDEV(rmc_major, 0);
    unregister_chrdev_region(devno, NR_OUTPUT_DEVICES);	

    if (class) {
        class_destroy(class);
        class = NULL;
    }
    if (hndl_proc_dir) {
        hnos_proc_rmdir();
    }

    HNOS_DEBUG_INFO("Cleanup %s, major %d \n", DEVICE_NAME, rmc_major);	
    return;
}


/*
 * Finally, the module stuff
 */
static int __init  rmc_module_init(void)
{
    int result = 0;
    dev_t dev = 0;

    result = alloc_chrdev_region(&dev, 0, NR_OUTPUT_DEVICES, DEVICE_NAME);
    rmc_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "hndl_rmc_device: can't get major %d\n", rmc_major);
        return result;
    }

    class = class_create(THIS_MODULE, DEVICE_NAME);
    if (!class){
        printk(KERN_ERR "Can't create class.\n");
        result = -1;
        goto fail;
    }


    HNOS_DEBUG_INFO("Initialized %s major as %d \n", DEVICE_NAME, rmc_major);	
    hndl_proc_dir = hnos_proc_mkdir();
    if (!hndl_proc_dir) {
        result = -ENODEV;
        goto fail;
    } 
    return result;

fail:
    HNOS_DEBUG_INFO("Initialize device %s failed.\n", DEVICE_NAME);

    rmc_module_cleanup();
    return result;

}


EXPORT_SYMBOL(rmc_device_alloc);
EXPORT_SYMBOL(rmc_device_free);
EXPORT_SYMBOL(rmc_device_register);
EXPORT_SYMBOL(rmc_device_unregister);
EXPORT_SYMBOL(rmc_smcbus_register);
EXPORT_SYMBOL(rmc_gpio_register);
EXPORT_SYMBOL(rmc_smcbus_unregister);
EXPORT_SYMBOL(rmc_gpio_unregister);
EXPORT_SYMBOL(rmc_smcbus_refresh_start);
EXPORT_SYMBOL(rmc_smcbus_refresh_stop);

module_init(rmc_module_init);
module_exit(rmc_module_cleanup);

MODULE_LICENSE("Dual BSD/GPL");


/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"
#include <linux/uaccess.h>

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;
static int device_open = 0;
MODULE_AUTHOR("Yara Mohsen"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
  loff_t newpos=0,file_size=0;
  uint8_t index=0;
  struct aesd_buffer_entry *entry;
 
 //Lock mutex
  if (mutex_lock_interruptible(&aesd_device.lock))
    return -ERESTARTSYS; 
  //get the current size of the buffer
  AESD_CIRCULAR_BUFFER_FOREACH(entry,&(aesd_device.circular_buffer),index) {
    file_size+=entry->size; 
  }
 
  mutex_unlock(&aesd_device.lock);
  newpos = fixed_size_llseek(filp, off, whence, file_size); 
  return newpos;
}

long aesd_adjust_file_offset(struct file *filp,unsigned int write_cmd,unsigned int write_cmd_offset)
{
  unsigned int newpos=0;
  long retval=0;
  // Lock mutex 
  if (mutex_lock_interruptible(&aesd_device.lock))
    return -ERESTARTSYS; 
 
  if ((write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)||(write_cmd_offset >= aesd_device.circular_buffer.entry[write_cmd].size)) 
  {
    retval= -EINVAL;  //write cmd or offset greater than the maximum possible size
    mutex_unlock(&aesd_device.lock);
    return retval;
  }
  for (int i = 0; i < write_cmd; i++) 
  { 
    newpos = newpos+ aesd_device.circular_buffer.entry[i].size;
  }
  
  filp->f_pos = newpos+ write_cmd_offset;
  retval= 0; 
  mutex_unlock(&aesd_device.lock);
  return retval;
}
 
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) 
{
  long retval=0;
  struct aesd_seekto seekto;
  switch (cmd) {
    case AESDCHAR_IOCSEEKTO:
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) //copy buffer from user
            return -EFAULT;
         else {
         	retval=aesd_adjust_file_offset(filp,seekto.write_cmd,seekto.write_cmd_offset); 
         	if(retval!=0)
         		return -EFAULT;	
         	}     
        break;
    default:
        return -ENOTTY;
        break;
    }
    return retval;
}

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    if (device_open)
        return -EBUSY;  // Return busy if already open
    
    //struct aesd_dev* dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = &aesd_device;
    
    PDEBUG("Device opened");
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    PDEBUG("Device released");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    mutex_lock(&aesd_device.lock);
    size_t entry_offset_byte_rtn;
    struct aesd_buffer_entry* entry = aesd_circular_buffer_find_entry_offset_for_fpos(&(aesd_device.circular_buffer),
            *f_pos, &entry_offset_byte_rtn );
            
    if(!entry)
    {
      if(entry_offset_byte_rtn == -1)// reached end of file
      {
          mutex_unlock(&aesd_device.lock);
          return 0;
      }
      PDEBUG("Read returned null");
      mutex_unlock(&aesd_device.lock);
      return -EINVAL;
    }
    PDEBUG("Read %s...", entry->buffptr);
    if(entry->size - entry_offset_byte_rtn < count) // will read the rest in another read call
    {
      if (copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, entry->size - entry_offset_byte_rtn)) return -EFAULT;
      *f_pos += entry->size - entry_offset_byte_rtn;
      mutex_unlock(&aesd_device.lock);
      return entry->size - entry_offset_byte_rtn;
    }
    
    if (copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, count)) return -EFAULT;
    *f_pos += count;
    
    mutex_unlock(&aesd_device.lock);
    return count;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    char *buffptr = kmalloc(count, GFP_KERNEL);
    if(!buffptr) return -ENOMEM;
    
    mutex_lock(&aesd_device.lock);
    
    if (copy_from_user(buffptr, buf, count)) {
        kfree(buffptr);
        mutex_unlock(&aesd_device.lock);
        return -EFAULT;
    }
    
    if (buffptr[count - 1] != '\n') { // partial write
        if(aesd_device.partial_data && aesd_device.partial_size > 0) // partial write is already in progress
        {
          aesd_device.partial_data = krealloc(aesd_device.partial_data, aesd_device.partial_size + count, GFP_KERNEL);
          if(!aesd_device.partial_data)
          {
            kfree(buffptr);
            mutex_unlock(&aesd_device.lock);
            return -EFAULT;
          }
          memcpy(aesd_device.partial_data + aesd_device.partial_size, buffptr, count);
          aesd_device.partial_size += count;
          kfree(buffptr);
          mutex_unlock(&aesd_device.lock);
          return count;
        }
        else // first partial write
        {
          aesd_device.partial_data = buffptr;
          aesd_device.partial_size = count;
          buffptr = NULL;
          mutex_unlock(&aesd_device.lock);
          return count;
        }
    }
        
    struct aesd_buffer_entry entry;// = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    if(aesd_device.partial_data && aesd_device.partial_size > 0) // there is an ongoing partial write and the last piece arrived
    { 
          aesd_device.partial_data = krealloc(aesd_device.partial_data, aesd_device.partial_size + count, GFP_KERNEL);
          if(!aesd_device.partial_data)
          {
            kfree(buffptr);
            mutex_unlock(&aesd_device.lock);
            return -EFAULT;
          }
          memcpy(aesd_device.partial_data + aesd_device.partial_size, buffptr, count);
          entry.buffptr= aesd_device.partial_data;
          entry.size = aesd_device.partial_size + count;
          aesd_device.partial_data = NULL;
          aesd_device.partial_size = 0;
          kfree(buffptr);
          buffptr=NULL;
    }
    else
    {
          entry.buffptr= buffptr;
          entry.size = count;
    }

    char* entry_del= aesd_circular_buffer_add_entry(&(aesd_device.circular_buffer), &entry);
    
    if(entry_del)
    {
      kfree(entry_del);
    }

    mutex_unlock(&aesd_device.lock);
    return count;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl =aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);
    aesd_circular_buffer_init(&(aesd_device.circular_buffer));
    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);
    ssize_t index;
    struct aesd_circular_buffer buffer;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&buffer,index) {kfree(entry->buffptr);}

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

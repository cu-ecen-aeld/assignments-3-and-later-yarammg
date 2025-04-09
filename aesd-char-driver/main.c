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
 * @references https://learning.oreilly.com/library/view/linux-device-drivers/0596005903/ch03.html#linuxdrive3-CHP-3-SECT-4.2
 *https://learning.oreilly.com/library/view/linux-device-drivers/0596005903/ch06.html#linuxdrive3-CHP-6-SECT-5.1
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h" //ioctl
 #include <linux/slab.h> 
#include <linux/uaccess.h>



int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Yara Mohsen"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;




/**************************OPEN********************************/
int aesd_open(struct inode *inode, struct file *filp)
{
struct aesd_dev *dev;
    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev); 
   filp->private_data = dev; //pointer to the aesd_dev structure is stored in the private data field of the file
    return 0;
}

/**************************RELEASE********************************/
int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    filp->private_data=NULL;//reverse of open, cleanup the pointer
    return 0;
}

/**************************READ********************************/
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset = 0;
    size_t bytes_to_copy=0;
  
    // Lock mutex before acessing the global data
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; 
    //a pointer to the buffer entry conataining the requested char offset(fpos)
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
   
       if (!entry)             //no data in buffer
       		goto out;
         
   bytes_to_copy = entry->size-entry_offset;//read from the offset till the end of the entry(<count)
    if (bytes_to_copy > count)     //if its greater than the requested count
        bytes_to_copy = count;  //then read the count 
        
        //read from entry offset 
      if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) { //returns 0 if successfully copied
        retval= -EFAULT;
        goto out;
        }
        
    *f_pos += bytes_to_copy; // Update file offset after successful read 
    retval = bytes_to_copy; //  number of bytes read
    goto out;
    
    out:  //cleanup
     mutex_unlock(&dev->lock);
     return retval; 

}

/*****************************LLSEEK****************************************/
loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev =filp->private_data;
    loff_t newpos=0,file_size=0;
   uint8_t index=0;
  struct aesd_buffer_entry *entry;

 // Lock mutex before acessing the global data
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; 
       
//calculate the current size of the file
  AESD_CIRCULAR_BUFFER_FOREACH(entry,&dev->buffer,index) {
   file_size+=entry->size; //summation of all the entry's size
   }
   mutex_unlock(&dev->lock);
   newpos = fixed_size_llseek(filp, off, whence, file_size); //find the offset
    return newpos;
}


/**************************WRITE********************************/
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev =filp->private_data;
    size_t bytes_to_copy=0;
    size_t bytes_to_write=0; //bytes written to the command buffer
    char *temp_buffer = NULL; //temporary buffer
    char *end_cmd =NULL; // pointer to hold newline 
   int new_line=0;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    temp_buffer = kmalloc(count, GFP_KERNEL);  // Allocate memory for the data count to write in temporary kernel buffer
    if (!temp_buffer)
        return -ENOMEM;
    bytes_to_copy=count;
        //copy from user buffer to kernel buffer   
   if(copy_from_user(temp_buffer,buf, bytes_to_copy))
   {
   retval= -EFAULT;
        goto out;
   } 
 
    //look for the end line character from the bytes received
    end_cmd = memchr(temp_buffer, '\n', count); 
    if (end_cmd) //returns pointer to the matching byte
        {bytes_to_write =  end_cmd - temp_buffer + 1; //difference of the pointers(location of the end of the command - start of the buffer)
         new_line=1; //newline is found
        }
   else // matching byte(newline) not found
   	bytes_to_write= count; //append everything 

 // Lock mutex before acessing the global data
    if (mutex_lock_interruptible(&dev->lock)){
        retval= -ERESTARTSYS; 
        goto out;              //if lock not acquired freeup
        }
        
 // reallocate the buffer to append new data
    char *new_buff  = krealloc(dev->entry.buffptr, dev->entry.size + bytes_to_write, GFP_KERNEL);
    if(!new_buff)  //malloc failed 
    {
        mutex_unlock(&dev->lock); //unlock and free previously allocated memory
        retval= -ENOMEM;
        goto out;
    }
    dev->entry.buffptr=new_buff;
 const void *dest_ptr=dev->entry.buffptr+ dev->entry.size; // destination pointer offset
 memcpy((void *)dest_ptr,temp_buffer,bytes_to_write); // no newline encountered , append the bytes into the current entry
  dev->entry.size= dev->entry.size+ bytes_to_write; //update the current entry size
    if(new_line) //add data to the circular buffer
    {
    const char *replaced_entry= aesd_circular_buffer_add_entry(&dev->buffer, &dev->entry);
    if (replaced_entry !=NULL)
    	kfree(replaced_entry); //free the overwritten entry
   dev->entry.size = 0; //reset the buffer entry
   dev->entry.buffptr = NULL;
    }    
    
   mutex_unlock(&dev->lock);  
   retval=count;
   goto out;
     
  out: 
    kfree(temp_buffer);
    return retval;
}

/**********************FOPS STRUCTURE**********************************************/
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release=  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl =aesd_ioctl,
};

/*******************************IOCTL*******************************************/
long aesd_adjust_file_offset(struct file *filp,unsigned int write_cmd,unsigned int write_cmd_offset)
{
   struct aesd_dev *dev =filp->private_data;
      loff_t newpos=0;
     long retval=0;
    // Lock mutex before acessing the global data
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; 
     
    if ((write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)||(write_cmd_offset >= dev->buffer.entry[write_cmd].size)) {
         retval= -EINVAL;  //write cmd or offset greater than the maximum possible size
         goto out;
    }

     for (int i = 0; i < write_cmd; i++) { //calculate the start offset to write cmd
        newpos = newpos+ dev->buffer.entry[i].size;
    }
    PDEBUG("adjusted the file offset");
    newpos+= write_cmd_offset;
    filp->f_pos = newpos;
    PDEBUG("IOCTL SEEK: write_cmd=%u, write_cmd_offset=%u, fpos=%lld\n",write_cmd, write_cmd_offset, newpos);
    retval= 0; 
    goto out;
    
  out: 
     mutex_unlock(&dev->lock);
    return retval;
}


long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) 
{
long retval=0;
    struct aesd_seekto seekto;
    switch (cmd) {
    case AESDCHAR_IOCSEEKTO:
    PDEBUG("AESDCHAR_IOCSEEKTO received:");
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) //copy buffer from user
            return -EFAULT;
         else {
         	retval=aesd_adjust_file_offset(filp,seekto.write_cmd,seekto.write_cmd_offset); 
         	if(retval!=0)
         		return -EFAULT;	
               else{
               	 PDEBUG("ioctl requests to change file offset and successfull");
               	 pr_info("iotcl successfully done");
               	 }
         }     
        break;
    default:
    PDEBUG("recieved 0x%x instead of  AESDCHAR_IOCSEEKTO\n",cmd);
        return -ENOTTY;
        break;
    }
    
    return retval;
}

/**************************SETUP********************************/
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

/**************************INITIALIZATION*******************************/
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
    
    //initialise the locking parameter
    mutex_init(&aesd_device.lock);
    //initialse the buffer
    aesd_circular_buffer_init(&aesd_device.buffer);
    
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

/**************************CLEANUP*******************************/
void aesd_cleanup_module(void)
{
 uint8_t index=0;
  struct aesd_buffer_entry *entry;
  AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.buffer,index) {
     kfree(entry->buffptr);   //free the memory of all the buffer entries
 }
    kfree(aesd_device.entry.buffptr);
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    cdev_del(&aesd_device.cdev);
    mutex_destroy(&aesd_device.lock);
    unregister_chrdev_region(devno, 1);
}    
    

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

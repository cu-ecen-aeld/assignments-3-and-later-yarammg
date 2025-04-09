/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  //Remove comment on this line to enable debug
#include "aesd-circular-buffer.h"

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev
{
    struct mutex lock;  // binary semaphore
    struct aesd_circular_buffer buffer; // structure to the buffer
    struct aesd_buffer_entry entry;  //structure to each entry  of the buffer
    struct cdev cdev;     // Char device structure 
};


int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos);
 ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos);
long aesd_ioctl(struct file *filp, unsigned int command, unsigned long arg);               
loff_t aesd_llseek(struct file *filp, loff_t off, int f_flags);    
long aesd_adjust_file_offset(struct file *filp,unsigned int write_cmd,unsigned int write_cmd_offset);            
static int aesd_setup_cdev(struct aesd_dev *dev);                             
 int aesd_init_module(void);
 void aesd_cleanup_module(void) ;
#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */

/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
  if (!buffer){ PDEBUG(" Buffer is null ");return NULL;}
  if ((buffer->in_offs == buffer->out_offs) && !buffer->full) {PDEBUG(" Buffer is empty "); return NULL;} //buffer is empty
  uint8_t index = buffer->out_offs;
  uint8_t count = 0;
  PDEBUG("Read requested, current in_offset %u, current out_offset %u", buffer->in_offs, buffer->out_offs); 
  while(count < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
  { 
    if(!buffer->full && index==buffer->in_offs) {PDEBUG(" Reached the end"); return NULL;} //reached the end
    
    struct aesd_buffer_entry *entry = &(buffer->entry[index]);
    if ( char_offset < entry->size) // it is in this entry
      {
        * entry_offset_byte_rtn = char_offset;
        PDEBUG("Reading %s...", entry->buffptr);
        return entry;
      }
      
      char_offset -= entry->size;
      count++;
      index = (index+1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
      
  }
    entry_offset_byte_rtn = -1; //end of file
    PDEBUG("Shouldnt reach here");
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
char * aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    char* to_free = NULL;
    if (!buffer) {PDEBUG(" Buffer is null"); return NULL;} // Safety check
    if (!add_entry) {PDEBUG(" Entry is null"); return NULL;} // Safety check
    
    if (buffer->full) {
        PDEBUG(" Buffer is full, returning the oldest entry to be freed");
        to_free =  buffer->entry[buffer->out_offs].buffptr;
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    // add entry
    //memcpy(&(buffer->entry[buffer->in_offs]), add_entry, sizeof(struct aesd_buffer_entry));
    buffer->entry[buffer->in_offs].buffptr=add_entry->buffptr;
    buffer->entry[buffer->in_offs].size=add_entry->size;
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if(buffer->in_offs == buffer->out_offs)
    {
      buffer->full = true;
    }
    
  PDEBUG(" Entry Added, current in_offset %u, current out_offset %u", buffer->in_offs, buffer->out_offs);  
  return to_free;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

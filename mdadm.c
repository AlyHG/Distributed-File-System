#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "net.h"

typedef struct JBOD{
  uint8_t currentBlockID;
  int8_t currentDiskID;
  uint8_t targetBlockID;
  int8_t targetDiskID;
  uint32_t block_pointer;
} JBOD;

JBOD jbod;  //Intializing the struct.
int mount_status = 1; //if mount_status = 1, it means disk is unmounted, if equal to 2, then disc is mounted.

//Block Constructor to create command block to use for system calls.
uint32_t block_constructor(uint8_t BlockID, uint16_t Reserved, uint8_t Disk_ID, uint8_t Command){
  uint32_t Reserved32;
  uint32_t Disk_ID32;
  uint32_t Command32;                               
  Reserved32 = (uint32_t) Reserved << 8;           
  Disk_ID32 = (uint32_t) Disk_ID << 22;             
  Command32 = (uint32_t) Command << 26;            
  uint32_t op = BlockID | Reserved32 | Disk_ID32 | Command32;
  return op;
}

int mdadm_mount(void) {
  //If Disc is Unmounted allow Mount. Otherwise System Call Fails. 
  if(mount_status==1){
    jbod_client_operation(JBOD_MOUNT, NULL);
    mount_status = 2;
    jbod.targetBlockID = 0;    
    jbod.targetDiskID = 0;
    jbod.block_pointer = 0;    
    return 1;
  }
  return -1;
}

int mdadm_unmount(void) {
  //If Disc is Mounted, allow Unmount. Otherwise System Call Fails.
  if(mount_status==2){
    jbod_client_operation(JBOD_UNMOUNT, NULL);
    mount_status = 1;
    return 1;
  }
  return -1;
}

//System call to go to specific block and/or Disk
int seek(uint8_t newBlockID,uint8_t newDiskID){
  //Check if disk is mounted. 
  if (mount_status == 1){
    return -1;
  }

  if(jbod.currentBlockID != newBlockID){
    //construct seek to block opcode
    uint32_t new_Block_op = block_constructor(newBlockID, 0, 0, JBOD_SEEK_TO_BLOCK);
    //If seek to block gives an error code, it will return -1, else it will just execute the system call
    if(jbod_client_operation(new_Block_op, NULL) != 0){
      return -1;
    }
  }

  if(jbod.currentDiskID != newDiskID){
    //construct seek to disk opcode
    uint32_t new_Disk_op = block_constructor(0, 0, newDiskID, JBOD_SEEK_TO_DISK);
    //If seek to disk gives an error code, it will return -1, else it will just execute the system call
    if (jbod_client_operation(new_Disk_op, NULL) != 0){
      return -1;
    }  
  }
  return 1; 
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  // If Disc is Unmounted, Dont Read, System Call Fails.
  if(mount_status==1){
    return -1;
  }  
  //If Test Any of the parameters do not meet the assignment/test requirements or is out of bounds, System Call Fails.
  if((len > 1024)|| (len < 0)||((len != 0) && (buf==NULL))||(addr + len > (JBOD_DISK_SIZE * JBOD_NUM_DISKS))){
    return -1;
  }
  // Instantiate local buffer of 256 Bytes that will act as source array for mem copy
  uint8_t localBuff[JBOD_BLOCK_SIZE];
  //Identify which disc the address is located in.
  jbod.targetDiskID = (addr / JBOD_DISK_SIZE);
  //Identify which block in the disc the address is located.
  jbod.targetBlockID = ((addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE);
  //Count variable that keeps track of number of bytes left to read.
  uint32_t length = len;
  //Identify where in the block to start reading from, (specific location of address in block)
  jbod.block_pointer = addr % JBOD_BLOCK_SIZE;

  // While bits left to be read is greater than 0
  while (length > 0){
    //seeks to block and disc of given address. 
    seek(jbod.targetBlockID, jbod.targetDiskID);
    //Construct Opcode
    uint32_t read_op = block_constructor(jbod.targetBlockID, 0, jbod.targetDiskID, JBOD_READ_BLOCK);
    
    //If Cache enabled
    if(cache_enabled() == true) {
      //Lookup cache entry for current disk and block, if found copies into local buffer
      if(cache_lookup(jbod.targetDiskID, jbod.targetBlockID, localBuff) == -1) {
        //If not found execute system call and isert into cache
        jbod_client_operation(read_op, localBuff);
        cache_insert(jbod.targetDiskID, jbod.targetBlockID, localBuff);
      }
    }else { //If cache not enabled
      //Execute System Call
      jbod_client_operation(read_op, localBuff);
    }

    //If addr + bytes to be read extends the bound of the current block:
    if(jbod.block_pointer + length > JBOD_BLOCK_SIZE-1){
      //Copy local Buffer (essentially current block) starting from the position of the block pointer(current address)
      memcpy(buf+(len-length),localBuff+jbod.block_pointer,JBOD_BLOCK_SIZE-jbod.block_pointer);
      //Update bytes left to be read
      length -= (JBOD_BLOCK_SIZE-jbod.block_pointer);
      //Check if the current block is the final block of the disc, if so increment disk number and set block id to zero.
      if(jbod.targetBlockID < JBOD_NUM_BLOCKS_PER_DISK - 1){
        jbod.targetBlockID ++;
      }else{
        jbod.targetDiskID ++;
        jbod.targetBlockID = 0;
      }
    //When moving on to next block set block pointer to zero to read from the beginning of the block.         
    jbod.block_pointer = 0;
    //If addr + bytes left to be read is within the the bounds of the current block.
    }else{
      memcpy(buf+(len-length),localBuff+jbod.block_pointer,length);
      length -= length;
    }
  }
//Once read is complete and copied to *buf, increment block by 1 for next I/O operation. If on final block of disc, move on to the beginning of the next disc.
  if(jbod.targetBlockID < JBOD_NUM_BLOCKS_PER_DISK - 1){
    jbod.targetBlockID ++;
  }else{
    jbod.targetDiskID ++;
    jbod.targetBlockID = 0;
  }
  jbod.block_pointer = 0; 
  return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
 // If Disc is Unmounted, Dont Write, System Call Fails.
  if(mount_status==1){
    return -1;
  }  
  //If Test Any of the parameters do not meet the assignment/test requirements or is out of bounds, System Call Fails.
  if((len > 1024)|| (len < 0)||((len != 0) && (buf==NULL))||(addr + len > (JBOD_DISK_SIZE * JBOD_NUM_DISKS))){
    return -1;
  }
  // Instantiate local buffer of 256 Bytes that will act as source array for mem copy
  uint8_t localBuff[JBOD_BLOCK_SIZE];
  //Identify which disc the address is located in.
  jbod.targetDiskID = (addr / JBOD_DISK_SIZE);
  //Identify which block in the disc the address is located.
  jbod.targetBlockID = ((addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE);
  //Count variable that keeps track of number of bytes left to read.
  uint32_t length = len;
  //Identify where in the block to start writing from, (specific location of address in block)
  jbod.block_pointer = addr % JBOD_BLOCK_SIZE;
  // While bits left to be written is greater than 0
  //mdadm_read(addr, len,)
  while (length > 0){
    //seeks to block and disc of given address. 
    seek(jbod.targetBlockID, jbod.targetDiskID);
    //Construct Read Opcode
    uint32_t read_op = block_constructor(jbod.targetBlockID, 0, jbod.targetDiskID, JBOD_READ_BLOCK);
    
    //Execute System Call which reads current block into localBuff
      jbod_client_operation(read_op, localBuff);
      //Seek again because read operation increments block internally.
      seek(jbod.targetBlockID, jbod.targetDiskID);

    //If addr + bytes to be written extends the bound of the current block:
    if(jbod.block_pointer + length > JBOD_BLOCK_SIZE-1){
      //Since LocalBuff is equivalent to current Block, add an offset of the block_pointer (current position in block) and from there I copied buf with an offset of number of bits read. 
      //I did that for a number of bits equivalent to the difference from current block position to the end of the current block.
      memcpy(localBuff+jbod.block_pointer, buf+(len-length), JBOD_BLOCK_SIZE-jbod.block_pointer);
      //Update bytes left to be read
      length -= (JBOD_BLOCK_SIZE-jbod.block_pointer);
      if(cache_enabled()){
        cache_insert(jbod.targetDiskID, jbod.targetBlockID, localBuff);
      }
      
      //Check if the current block is the final block of the disc, if so increment disk number and set block id to zero.
      if(jbod.targetBlockID < JBOD_NUM_BLOCKS_PER_DISK - 1){
        jbod.targetBlockID ++;
      //otherwise just increment the block ID.
      }else{
        jbod.targetDiskID ++;
        jbod.targetBlockID = 0;
      }
    //When moving on to next block set block pointer to zero to read from the beginning of the block.         
    jbod.block_pointer = 0;
    //If addr + bytes left to be read is within the the bounds of the current block.
    }else{
      memcpy(localBuff+jbod.block_pointer, buf+(len-length), length);
      length -= length;
    }
    //construct opcode for write in current block jbod operation.
    uint32_t write_op = block_constructor(jbod.targetBlockID, 0, jbod.targetDiskID, JBOD_WRITE_BLOCK);
    //execute jbod operation where current block in Disk is rewritten from localBuff
    jbod_client_operation(write_op,localBuff);
    
    //If Cache enabled, Update Cache with data in current block 
    if (cache_enabled()){
      cache_update(jbod.targetDiskID, jbod.targetBlockID, localBuff);
    }
  }
  //Once write is complete and copied to current block, increment block by 1 for next I/O operation. If on final block of disc, move on to the beginning of the next disc.
  if(jbod.targetBlockID < JBOD_NUM_BLOCKS_PER_DISK - 1){
    jbod.targetBlockID ++;
  }else{
    jbod.targetDiskID ++;
    jbod.targetBlockID = 0;
  }
  jbod.block_pointer = 0; 
  return len;
}

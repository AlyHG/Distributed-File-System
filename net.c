#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"
 
/* the client socket descriptor for the connection to the server */
int cli_sd = -1;
int lengthSize = 2;
int opSize = 4;
int retSize = 2;
 
 
/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  int bytesRead = 0; //Counter variable for bytes read
  int loopResult; //Bytes written as a result of the  while loop
  while (bytesRead < len){
    loopResult = read(fd, buf + bytesRead, len-bytesRead); //Reading from file descriptor
    //If read fails return false
    if(loopResult < 0){
      return false;
    }
    //Incrementing counter variable
    bytesRead += loopResult;
  }
  return true;
}
 
/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  int bytesWritten = 0; //Counter variable for bytes written
  int loopResult; //Bytes written as a result of the  while loop
  while (bytesWritten < len){
    loopResult = write(fd, buf + bytesWritten, len-bytesWritten); //Reading from file descriptor
    //If read fails return false
    if(loopResult < 0){
      return false;
    }
    //Incrementing counter variable
    bytesWritten += loopResult;
  }
  return true;
}
/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 
 
op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)
 
In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  uint8_t headbuff[HEADER_LEN]; //Array containing header content
  uint16_t length;  //Length of Host, 
  uint16_t nLength; //Length of Network
  uint16_t nReturn; //return 
  uint32_t nOp; //Network opcode
 
  int headOffset = 0; //Offset used when reading from header
 
  //If unable to read Header return false
  if (nread(sd, HEADER_LEN, headbuff) == false){
    return false;
  }
 
  //Get the length, opcode, and return from the header.
  memcpy(&nLength, headbuff + headOffset, lengthSize);
  headOffset += lengthSize;
  memcpy(&nOp, headbuff + headOffset, opSize);
  headOffset += opSize;
  memcpy(&nReturn, headbuff + headOffset, retSize);
 
  //Assigning network values to host values
  length = ntohs(nLength);
  *ret = ntohs(nReturn);
  *op = ntohl(nOp);
 
  //When reading the block, set the size of read to JBOD Block Size
  if (length > (HEADER_LEN)){
    if (nread(sd, JBOD_BLOCK_SIZE, block) == false){
      return false;
    }
  }
  return true; 
}
 
 
 
/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 
 
op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.
 
The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  uint8_t buff[HEADER_LEN + JBOD_BLOCK_SIZE]; // Array containing Packet
  uint16_t length = HEADER_LEN; // Host length
  uint16_t nLength; // Network length
  uint32_t nOp; // Network op
  uint32_t cmd = op >> 26; // Bit shifting opcode 26 to the right to get cmd 
 
  //If cmd is a is the code for write system call add block size to the length
  if(cmd == JBOD_WRITE_BLOCK){
    length += JBOD_BLOCK_SIZE;
    memcpy(buff + HEADER_LEN, block, JBOD_BLOCK_SIZE);
  }
 
  //Assigning network values to host values
  nLength = htons(length);
  nOp = htonl(op);
 
  int headOffset = 0; //Offset used when reading from header
  // Create Header via memcopying into packet using nlength and nOp 
  memcpy(buff + headOffset, &nLength, lengthSize);
  headOffset += lengthSize;
  memcpy(buff + headOffset, &nOp, opSize);
  return nwrite(sd, length, buff);
}
 
 
 
/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  //Create socket
  cli_sd = socket(PF_INET, SOCK_STREAM, 0);
 
  //If client socket descriptor = -1 return false as its unable to establish a connection
  if(cli_sd == -1)
    return false;
 
  // Setting up the IP address (covered in lecture)
  struct sockaddr_in ipv4_addr;
  ipv4_addr.sin_family = AF_INET;
  ipv4_addr.sin_port = htons(port);
 
  //If unable to convert address
  if(inet_aton(ip, &ipv4_addr.sin_addr) == 0){
    return false;
  }
 
  // Connect socket
  if(connect(cli_sd, (const struct sockaddr *)&ipv4_addr, sizeof(ipv4_addr)) == -1){
    //If unable to connect
    return false;
  }else{
  //If Connection successful
    return true;
  }
}
 
/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  // Close client side descriptor for server
  close(cli_sd);
  //Mark as closed
  cli_sd = -1;
}
 
/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 
 
The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint16_t ret; //Input  for recv_packet function
 
  // Send the JBOD operation to the server
  if(send_packet(cli_sd, op, block) == false){
    //if unable to send packet
    return -1;
  }
 
  //If unable to recieve packet (response) return -1
  if(recv_packet(cli_sd, &op, &ret, block) == false){
    return -1;
  }
  else{
    return ret;
  }
}
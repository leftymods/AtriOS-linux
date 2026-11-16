/* Compile against the production wire struct and checksum helpers. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "f_aml_burn.h"
_Static_assert(sizeof(struct CmdData_WriteMedia) == 32, "wire size");
_Static_assert(offsetof(struct CmdData_WriteMedia, userDataLen) == 4, "length offset");
_Static_assert(offsetof(struct CmdData_WriteMedia, sequenceNo) == 8, "sequence offset");
_Static_assert(offsetof(struct CmdData_WriteMedia, dataCheckSum) == 12, "checksum offset");
_Static_assert(offsetof(struct CmdData_WriteMedia, dataCheckAlgorithm) == 16, "algorithm offset");
int main(void)
{
 uint8_t bytes[1031];
 uint32_t state=7, n, i, ref;
 struct CmdData_WriteMedia hdr;
 const uint8_t packet[32]={0,0,0,0,0,0,1,0,42,0,0,0,0x78,0x56,0x34,0x12,0xef,0,0,2};
 memcpy(&hdr, packet, 32);
 assert(hdr.userDataLen==65536 && hdr.sequenceNo==42);
 assert(hdr.dataCheckSum==0x12345678 && hdr.dataCheckAlgorithm==0xef);
 assert(aml_valid_chunk(65536, 0xef, 512));
 assert(!aml_valid_chunk(0,0xef,512));
 assert(!aml_valid_chunk(AML_MAX_CHUNK+1,0xef,512));
 assert(!aml_valid_chunk(UINT32_MAX,0xef,512));
 assert(!aml_valid_chunk(512,0x9999,512));
 assert(!aml_valid_chunk(512,0xef,0));
 assert(aml_valid_write(512,512,1024,512));
 assert(!aml_valid_write(0,1,1024,512));
 assert(!aml_valid_write(1,512,1024,512));
 assert(!aml_valid_write(512,1024,1024,512));
 assert(!aml_valid_write(UINT64_MAX-511,512,UINT64_MAX,512));
 assert(!aml_valid_write(0,512,1024,0));
 for(i=0;i<sizeof(bytes);i++){state=state*1664525+1013904223;bytes[i]=state>>24;}
 for(n=0;n<1028;n++) {
  ref=0;
  for(i=0;i<n;i++)ref+=(uint32_t)bytes[i+1]<<((i%4)*8);
  assert(aml_addsum(bytes+1,n)==ref);
 }
 assert(aml_addsum((const uint8_t *)"\1\2",2)==513);
 return 0;
}

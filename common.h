#include _WITCH_PATH(MEM/MEM.h)
#include _WITCH_PATH(STR/common/common.h)
#include _WITCH_PATH(IO/IO.h)
#include _WITCH_PATH(IO/print.h)
#include _WITCH_PATH(HASH/SHA.h)
#include _WITCH_PATH(RAND/RAND.h)
#include _WITCH_PATH(VEC/VEC.h)
#include _WITCH_PATH(EV/EV.h)
#include _WITCH_PATH(NET/NET.h)
#include _WITCH_PATH(NET/TCP/TCP.h)

#include "prot.h"

void _print(uint32_t fd, const char *format, ...){
  IO_fd_t fd_stdout;
  IO_fd_set(&fd_stdout, fd);
  va_list argv;
  va_start(argv, format);
  IO_vprint(&fd_stdout, format, argv);
  va_end(argv);
}
#define WriteInformation(...) _print(FD_OUT, __VA_ARGS__)
#define WriteError(...) _print(FD_ERR, __VA_ARGS__)

bool IsInputLineDone(uint8_t **buffer, IO_ssize_t *size){
  for(uintptr_t i = 0; i < *size; i++){
    if((*buffer)[i] == 0x7f || (*buffer)[i] == 0x08){
      if(g_pile->InputSize){
        g_pile->InputSize--;
      }
      continue;
    }
    if((*buffer)[i] == '\n' || (*buffer)[i] == '\r'){
      if((*buffer)[i] == '\r'){
        WriteInformation("\r\n");
      }
      *buffer += i + 1;
      *size -= i + 1;
      return 1;
    }
    g_pile->Input[g_pile->InputSize++] = (*buffer)[i];
  }
  return 0;
}

void TCP_write_DynamicPointer(NET_TCP_peer_t *peer, void *Data, uintptr_t Size){
  NET_TCP_Queue_t Queue;
  Queue.DynamicPointer.ptr = Data;
  Queue.DynamicPointer.size = Size;
  NET_TCP_write_loop(
    peer,
    NET_TCP_GetWriteQueuerReferenceFirst(peer),
    NET_TCP_QueueType_DynamicPointer,
    &Queue);
}

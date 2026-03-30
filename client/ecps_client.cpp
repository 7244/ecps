#include <WITCH/WITCH.h>
#define ETC_VEDC_Encode_DefineEncoder_OpenH264
#define ETC_VEDC_Encode_DefineEncoder_x264
#if defined(__platform_windows)
  #define ETC_VEDC_Encode_DefineEncoder_nvenc
#endif

#define ETC_VEDC_Decoder_DefineCodec_OpenH264
#if defined(__platform_windows)
  #define ETC_VEDC_Decoder_DefineCodec_cuvid
#endif

#include "types.h"

void TCP_WriteCommand(uint32_t ID, Protocol_CI_t Command, auto&&...args){
  ProtocolBasePacket_t BasePacket;
  BasePacket.ID = ID;
  BasePacket.Command = Command;
  TCP_write_DynamicPointer(g_pile->TCP.peer, &BasePacket, sizeof(BasePacket));
  (
    TCP_write_DynamicPointer(g_pile->TCP.peer, &args, sizeof(args)),
  ...);
}

template <typename T>
void
UDP_send(
  uint32_t ID,
  const T& Command,
  T CommandData,
  const void *data,
  IO_size_t size
){
  uint8_t buffer[2048];
  auto BasePacket = (ProtocolUDP::BasePacket_t *)buffer;
  BasePacket->SessionID = g_pile->SessionID;
  BasePacket->ID = ID;
  BasePacket->IdentifySecret = g_pile->UDP.IdentifySecret;
  BasePacket->Command = Command;
  auto CommandDataPacket = (T *)&BasePacket[1];
  *CommandDataPacket = CommandData;
  auto RestPacket = (uint8_t *)CommandDataPacket + T::dss;
  __builtin_memcpy(RestPacket, data, size);
  uint16_t TotalSize = sizeof(ProtocolUDP::BasePacket_t) + T::dss + size;
  IO_ssize_t r = NET_sendto(&g_pile->UDP.udp, buffer, TotalSize, &g_pile->UDP.Address);
  if(r != TotalSize){
    WriteInformation(
      "[INTERNAL ERROR] %s %s:%lu NET_sendto failed. wanted %x got %x\r\n",
      __FUNCTION__, __FILE__, __LINE__, TotalSize, r);
  }
}

template <typename ...T>
void
ITC_write(
  Protocol_CI_t Command,
  Protocol_ChannelID_t ChannelID,
  uint32_t ChannelUnique,
  T ...args
){
  struct _t{
    _t(
      const Protocol_CI_t &Command,
      const Protocol_ChannelID_t &ChannelID,
      const uint32_t &ChannelUnique
    ) : BasePacket({.Command = Command, .ChannelID = ChannelID, .ChannelUnique = ChannelUnique}) {}
    ITCBasePacket_t BasePacket;
    uint8_t x[(sizeof(T) + ... + 0)]{0};
  }Pack(Command, ChannelID, ChannelUnique);
  {
    uint32_t i = 0;
    constexpr auto l = [](auto& pack, auto&i, auto args) {
      *(decltype(args) *)&pack.x[i] = args;
      i += sizeof(args);
    };
    (l(Pack, i, args), ...);
  }

  TH_lock(&g_pile->ITC.Mutex);
  auto Current = g_pile->ITC.bvec.Current;
  pile_t::ITC_t::bvec_AddEmpty(&g_pile->ITC.bvec, sizeof(Pack));
  __builtin_memcpy(&g_pile->ITC.bvec.ptr[Current], &Pack, sizeof(Pack));
  TH_unlock(&g_pile->ITC.Mutex);
  EV_async_send(&g_pile->listener, &g_pile->ITC.ev_async);
}

#define UDP_KeepAliveTimerStepLimit 7
uint8_t UDP_KeepAliveTimerStep[UDP_KeepAliveTimerStepLimit] = {30, 1, 2, 2, 4, 4, 4};
void UDP_KeepAliveTimer_cb(EV_t *listener, EV_timer_t *KeepAliveTimer){
  UDP_send(0, ProtocolUDP::C2S_t::KeepAlive, {}, 0, 0);

  if(g_pile->UDP.KeepAliveTimerStepCurrent == UDP_KeepAliveTimerStepLimit - 1){
    WriteInformation("[CLIENT] UDP KeepAlive timeout\r\n");
  }
  else{
    g_pile->UDP.KeepAliveTimerStepCurrent++;
  }
  EV_timer_stop(listener, KeepAliveTimer);
  EV_timer_init(KeepAliveTimer, UDP_KeepAliveTimerStep[g_pile->UDP.KeepAliveTimerStepCurrent], UDP_KeepAliveTimer_cb);
  EV_timer_start(listener, KeepAliveTimer);
}
void UDP_KeepAliveTimer_reset(){
  g_pile->UDP.KeepAliveTimerStepCurrent = 0;
  EV_timer_stop(&g_pile->listener, &g_pile->UDP.KeepAliveTimer);
  EV_timer_init(&g_pile->UDP.KeepAliveTimer, UDP_KeepAliveTimerStep[0], UDP_KeepAliveTimer_cb);
  EV_timer_start(&g_pile->listener, &g_pile->UDP.KeepAliveTimer);
}
void UDP_KeepAliveTimer_start(){
  g_pile->UDP.KeepAliveTimerStepCurrent = 0;
  EV_timer_init(&g_pile->UDP.KeepAliveTimer, 0, UDP_KeepAliveTimer_cb);
  EV_timer_start(&g_pile->listener, &g_pile->UDP.KeepAliveTimer);
}

#define TCP_KeepAliveTimerStepLimit 7
uint8_t TCP_KeepAliveTimerStep[TCP_KeepAliveTimerStepLimit] = {30, 1, 2, 2, 4, 4, 4};
void TCP_KeepAliveTimer_cb(EV_t *listener, EV_timer_t *KeepAliveTimer){
  TCP_WriteCommand(
    0,
    Protocol_C2S_t::KeepAlive);

  if(g_pile->TCP.KeepAliveTimerStepCurrent == TCP_KeepAliveTimerStepLimit - 1){
    WriteInformation("[CLIENT] TCP KeepAlive timeout\r\n");
  }
  else{
    g_pile->TCP.KeepAliveTimerStepCurrent++;
  }
  EV_timer_stop(listener, KeepAliveTimer);
  EV_timer_init(KeepAliveTimer, TCP_KeepAliveTimerStep[g_pile->TCP.KeepAliveTimerStepCurrent], TCP_KeepAliveTimer_cb);
  EV_timer_start(listener, KeepAliveTimer);
}
void TCP_KeepAliveTimer_reset(){
  g_pile->TCP.KeepAliveTimerStepCurrent = 0;
  EV_timer_stop(&g_pile->listener, &g_pile->TCP.KeepAliveTimer);
  EV_timer_init(&g_pile->TCP.KeepAliveTimer, TCP_KeepAliveTimerStep[0], TCP_KeepAliveTimer_cb);
  EV_timer_start(&g_pile->listener, &g_pile->TCP.KeepAliveTimer);
}
void TCP_KeepAliveTimer_start(){
  g_pile->TCP.KeepAliveTimerStepCurrent = 0;
  EV_timer_init(&g_pile->TCP.KeepAliveTimer, TCP_KeepAliveTimerStep[0], TCP_KeepAliveTimer_cb);
  EV_timer_start(&g_pile->listener, &g_pile->TCP.KeepAliveTimer);
}

#include "ScreenShare/ScreenShare.h"

uint32_t
TCPMain_state_cb(
  NET_TCP_peer_t *peer,
  TCPMain_SockData_t *SockData,
  TCPMain_PeerData_t *PeerData,
  uint32_t flag
){
  if(flag & NET_TCP_state_succ_e){
    NET_TCP_StartReadLayer(peer, SockData->ReadLayerID);

    WriteInformation("[CLIENT] connection success with %08lx%04lx\r\n", peer->sdstaddr.ip, peer->sdstaddr.port);

    g_pile->state = PileState_t::Connected;
    PeerData->state = TCPPeerState_t::Idle;

    g_pile->UDP.Address.ip = peer->sdstaddr.ip;
    g_pile->UDP.Address.port = peer->sdstaddr.port;

    g_pile->UDP.IdentifySecret = 0;

    TCP_KeepAliveTimer_start();

    Protocol_C2S_t::Request_Login_t rest;
    rest.Type = Protocol::LoginType_t::Anonymous;

    uint32_t ID = g_pile->TCP.IDSequence++;

    { /* TODO use actual output instead of filler */
      uint8_t filler = 0;
      IDMap_InNew(&g_pile->TCP.IDMap, &ID, &filler);
    }

    TCP_WriteCommand(
      ID,
      Protocol_C2S_t::Request_Login,
      rest);
  }
  else do{
    if(!(flag & NET_TCP_state_init_e)){
      WriteInformation("[CLIENT] connection failed with %08lx%04lx\r\n", peer->sdstaddr.ip, peer->sdstaddr.port);
      g_pile->state = PileState_t::NotConnected;
      break;
    }
    WriteInformation("[CLIENT] connection dropped with %08lx%04lx\r\n", peer->sdstaddr.ip, peer->sdstaddr.port);
    /* good luck to clean */
    __abort();
  }while(0);

  return 0;
}

uint32_t TCPMain_read_cb(
  NET_TCP_peer_t *peer,
  uint8_t *SockData,
  TCPMain_PeerData_t *PeerData,
  NET_TCP_QueuerReference_t QueuerReference,
  uint32_t *type,
  NET_TCP_Queue_t *Queue
){
  uint8_t *ReadData;
  uintptr_t ReadSize;
  uint8_t _EventReadBuffer[4096];
  switch(*type){
    case NET_TCP_QueueType_DynamicPointer:{
      ReadData = (uint8_t *)Queue->DynamicPointer.ptr;
      ReadSize = Queue->DynamicPointer.size;
      break;
    }
    case NET_TCP_QueueType_PeerEvent:{
      IO_fd_t peer_fd;
      EV_event_get_fd(&peer->event, &peer_fd);
      IO_ssize_t len = IO_read(&peer_fd, _EventReadBuffer, sizeof(_EventReadBuffer));
      if(len < 0){
        NET_TCP_CloseHard(peer);
        return NET_TCP_EXT_PeerIsClosed_e;
      }
      ReadData = _EventReadBuffer;
      ReadSize = len;
      break;
    }
    case NET_TCP_QueueType_CloseHard:{
      return 0;
    }
    default:{
      WriteError("*type %lx\r\n", *type);
      __abort();
    }
  }
  NET_TCP_UpdatePeerTimer(peer->parent->listener, peer->parent, peer);
  for(uintptr_t iSize = 0; iSize < ReadSize;){
    switch(PeerData->state){
      case TCPPeerState_t::Idle:{
        PeerData->iBuffer = 0;
        PeerData->Buffer = A_resize(0, sizeof(ProtocolBasePacket_t));
        PeerData->state = TCPPeerState_t::Waitting_BasePacket;
        break;
      }
      case TCPPeerState_t::Waitting_BasePacket:{
        uintptr_t size = ReadSize - iSize;
        uintptr_t needed_size = sizeof(ProtocolBasePacket_t) - PeerData->iBuffer;
        if(size > needed_size){
          size = needed_size;
        }
        __builtin_memcpy(&PeerData->Buffer[PeerData->iBuffer], &ReadData[iSize], size);
        iSize += size;
        PeerData->iBuffer += size;
        if(PeerData->iBuffer == sizeof(ProtocolBasePacket_t)){
          auto BasePacket = (ProtocolBasePacket_t *)PeerData->Buffer;
          if(BasePacket->Command >= Protocol_S2C_t::GetMemberAmount()){
            WriteInformation("[SERVER ERROR] BasePacket->Command >= Protocol_S2C_t::GetMemberAmount()\r\n");
            A_resize(PeerData->Buffer, 0);
            NET_TCP_CloseHard(peer);
            return NET_TCP_EXT_PeerIsClosed_e;
          }
          PeerData->Buffer = A_resize(PeerData->Buffer, sizeof(ProtocolBasePacket_t) + Protocol_S2C.NA(BasePacket->Command)->m_DSS);
          PeerData->state = TCPPeerState_t::Waitting_Data;
        }
        break;
      }
      case TCPPeerState_t::Waitting_Data:{
        auto BasePacket = (ProtocolBasePacket_t *)PeerData->Buffer;
        uintptr_t TotalSize = sizeof(ProtocolBasePacket_t) + Protocol_S2C.NA(BasePacket->Command)->m_DSS;
        uintptr_t size = ReadSize - iSize;
        uintptr_t needed_size = TotalSize - PeerData->iBuffer;
        if(size > needed_size){
          size = needed_size;
        }
        __builtin_memcpy(&PeerData->Buffer[PeerData->iBuffer], &ReadData[iSize], size);
        iSize += size;
        PeerData->iBuffer += size;
        if(PeerData->iBuffer == TotalSize){
          auto RestPacket = (uint8_t *)BasePacket + sizeof(ProtocolBasePacket_t);
          if(BasePacket->Command == Protocol_S2C_t::KeepAlive){
            TCP_KeepAliveTimer_reset();
            goto StateDone_gt;
          }
          switch(g_pile->state){
            case PileState_t::NotConnected:
            case PileState_t::Connecting:
            { // not possible to come
              break;
            }
            case PileState_t::Connected:
            {
              switch(BasePacket->Command){
                #include "ReadDone/Connected.h"
              }
              break;
            }
            case PileState_t::Logined:
            {
              switch(BasePacket->Command){
                #include "ReadDone/Logined.h"
              }
              break;
            }
          }
        }
        break;
      }
    }
    continue;
    StateDone_gt:
    A_resize(PeerData->Buffer, 0);
    PeerData->state = TCPPeerState_t::Idle;
  }
  return 0;
}

uintptr_t GetSizeOfArgument(const uint8_t *Command, uintptr_t *iCommand, uintptr_t CommandSize){
  for(; *iCommand < CommandSize; (*iCommand)++){
    if(!STR_ischar_blank(Command[*iCommand])){
      break;
    }
  }
  uintptr_t iCommandLocal = *iCommand;
  for(; iCommandLocal < CommandSize; iCommandLocal++){
    if(STR_ischar_blank(Command[iCommandLocal])){
      return iCommandLocal - *iCommand;
    }
  }
  return iCommandLocal - *iCommand;
}

bool CompareCommand(const uint8_t *Input, uintptr_t *iInput, uintptr_t InputSize, const void *Str){
  for(; *iInput < InputSize; (*iInput)++){
    if(!STR_ischar_blank(Input[*iInput])){
      break;
    }
  }
  uintptr_t CommandSize = GetSizeOfArgument(Input, iInput, InputSize);
  uintptr_t StrSize = MEM_cstrlen(Str);
  if(StrSize != CommandSize){
    return 0;
  }
  if(STR_ncasecmp(&Input[*iInput], Str, StrSize)){
    return 0;
  }
  return 1;
}

bool GetNextArgument(const uint8_t *Command, uintptr_t *iCommand, uintptr_t CommandSize){
  for(; *iCommand < CommandSize; (*iCommand)++){
    if(!STR_ischar_blank(Command[*iCommand])){
      break;
    }
  }
  for(; *iCommand < CommandSize; (*iCommand)++){
    if(STR_ischar_blank(Command[*iCommand])){
      break;
    }
  }
  for(; *iCommand < CommandSize; (*iCommand)++){
    if(!STR_ischar_blank(Command[*iCommand])){
      return 1;
    }
  }
  return 0;
}

#include "Input.h"

bool IsInputLineDone(uint8_t **buffer, uintptr_t *size){
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

void evio_stdin_cb(EV_t *listener, EV_event_t *evio_stdin, uint32_t flag){
  uint8_t _buffer[4096];
  uint8_t *buffer = _buffer;
  IO_ssize_t size;
  IO_fd_t fd;
  EV_event_get_fd(evio_stdin, &fd);
  size = IO_read(&fd, buffer, sizeof(_buffer));
  if(size < 0){
    __abort();
  }
  while(1){
    if(!IsInputLineDone(&buffer, (uintptr_t *)&size)){
      return;
    }
    ProcessInput(g_pile->Input, g_pile->InputSize);
    g_pile->InputSize = 0;
  }
}

void ev_udp_read_cb(EV_t *listener, EV_event_t *evio_udp, uint32_t flag){
  uint8_t buffer[0x800];
  NET_addr_t dstaddr;
  IO_ssize_t size = NET_recvfrom(&g_pile->UDP.udp, buffer, sizeof(buffer), &dstaddr);
  if(size < 0){
    if(size == -EAGAIN){
      return;
    }
    WriteInformation("%x\n", size);
    __abort();
  }
  if(dstaddr.ip != g_pile->UDP.Address.ip || dstaddr.port != g_pile->UDP.Address.port){
    /* packet came from elsewhere */
    return;
  }
  // TODO
  // InChannel_ScreenShare_View_KeepAliveTimer_reset(View);
  if(size == 0){
    return;
  }
  if(size == sizeof(buffer)){
    WriteInformation("where is mtu for this packet??\n");
    __abort();
  }

  if((uintptr_t)size < sizeof(ProtocolUDP::BasePacket_t)){
    return;
  }
  auto BasePacket = (ProtocolUDP::BasePacket_t *)buffer;

  if(BasePacket->SessionID != g_pile->SessionID){
    return;
  }

  uintptr_t RelativeSize = size - sizeof(*BasePacket);
  switch(BasePacket->Command){
    case ProtocolUDP::S2C_t::KeepAlive:{

      UDP_KeepAliveTimer_reset();

      return;
    }
    case ProtocolUDP::S2C_t::Channel_ScreenShare_View_StreamData:{
      auto CommandData = (ProtocolUDP::S2C_t::Channel_ScreenShare_View_StreamData_t *)&BasePacket[1];
      if(RelativeSize < sizeof(*CommandData)){
        return;
      }
      RelativeSize -= sizeof(*CommandData);

      Channel_ScreenShare_View_t *View;
      Protocol_ChannelID_t ChannelID = CommandData->ChannelID;
      {
        auto cc = ChannelMap_GetOutputPointerSafe(&g_pile->ChannelMap, &ChannelID);
        if(cc == NULL){
          __abort();
          return;
        }
        switch(cc->GetState()){
          case ChannelState_t::ScreenShare_View:{
            break;
          }
          default:{
            return;
          }
        }
        View = (Channel_ScreenShare_View_t *)cc->m_StateData;
      }

      if(RelativeSize < sizeof(ScreenShare_StreamHeader_Body_t)){
        return;
      }
      auto StreamData = (uint8_t *)&CommandData[1];

      auto Body = (ScreenShare_StreamHeader_Body_t *)StreamData;
      uint16_t Sequence = Body->GetSequence();
      if(Sequence != View->m_Sequence){
        if(View->IsSequencePast(Sequence)){
          /* this packet came from past */
          return;
        }
        View->FixFramePacket();
        if(View->m_Possible != (uint16_t)-1){
          View->WriteFramePacket();
        }
        View->SetNewSequence(Sequence);
      }
      uint8_t *PacketData;
      uint16_t Current = Body->GetCurrent();
      if(Current == 0){
        if((uintptr_t)size < sizeof(ScreenShare_StreamHeader_Head_t)){
          return;
        }
        if(View->m_Possible != (uint16_t)-1){
          /* we already have head somehow */
          return;
        }
        auto Head = (ScreenShare_StreamHeader_Head_t *)StreamData;
        #if set_VerboseProtocol_HoldStreamTimes == 1
          View->_VerboseTime = T_nowi();
          WriteInformation("[CLIENT] [DEBUG] head came\r\n");
          WriteInformation("  %llu %llu\r\n", Head->_VerboseTime.ScreenRead, Head->_VerboseTime.SourceOptimize - Head->_VerboseTime.ScreenRead);
          WriteInformation("  %llu %llu\r\n", Head->_VerboseTime.SourceOptimize, Head->_VerboseTime.Encode - Head->_VerboseTime.SourceOptimize);
          WriteInformation("  %llu %llu\r\n", Head->_VerboseTime.Encode, Head->_VerboseTime.WriteQueue - Head->_VerboseTime.Encode);
          WriteInformation("  %llu %llu\r\n", Head->_VerboseTime.WriteQueue, Head->_VerboseTime.ThreadFrameEnd - Head->_VerboseTime.WriteQueue);
          WriteInformation("  %llu %llu\r\n", Head->_VerboseTime.ThreadFrameEnd, Head->_VerboseTime.NetworkWrite - Head->_VerboseTime.ThreadFrameEnd);
          WriteInformation("  %llu %llu\r\n", Head->_VerboseTime.NetworkWrite, View->_VerboseTime - Head->_VerboseTime.NetworkWrite);
        #endif
        View->m_Possible = Head->GetPossible();
        /*
        View->m_Flag = Head->GetFlag();
        */
        PacketData = &StreamData[sizeof(ScreenShare_StreamHeader_Head_t)];
      }
      else{
        PacketData = &StreamData[sizeof(ScreenShare_StreamHeader_Body_t)];
      }

      uint16_t PacketSize = RelativeSize - ((uintptr_t)PacketData - (uintptr_t)StreamData);

      if(PacketSize != 0x400){
        View->m_ModuloSize = PacketSize;
      }
      __builtin_memcpy(&View->m_data[Current * 0x400], PacketData, PacketSize);
      View->SetDataCheck(Current);
      if(
        (View->m_Possible != (uint16_t)-1 && Current == View->m_Possible) ||
        PacketSize != 0x400
      ){
        View->FixFramePacket();
        if(View->m_Possible != (uint16_t)-1){
          View->WriteFramePacket();
        }
        View->SetNewSequence((View->m_Sequence + 1) % 0x1000);
      }

      break;
    }
  }
}

void ITC_read_cb(EV_t *listener, EV_async_t *async){
  TH_lock(&g_pile->ITC.Mutex);
  for(uint32_t i = 0; i < g_pile->ITC.bvec.Current;){
    auto BasePacket = (ITCBasePacket_t *)&g_pile->ITC.bvec.ptr[i];
    i += sizeof(ITCBasePacket_t);

    auto cc = ChannelMap_GetOutputPointerSafe(&g_pile->ChannelMap, &BasePacket->ChannelID);
    if(cc == NULL){
      i += ITC_Protocol.NA(BasePacket->Command)->m_DSS;
      continue;
    }

    if(cc->m_ChannelUnique != BasePacket->ChannelUnique){
      i += ITC_Protocol.NA(BasePacket->Command)->m_DSS;
      continue;
    }

    switch(BasePacket->Command){
      case ITC_Protocol_t::CloseChannel:{
        __abort();
        break;
      }
      case ITC_Protocol_t::Channel_ScreenShare_Share_MouseCoordinate:{
        auto CommandData = (ITC_Protocol_t::Channel_ScreenShare_Share_MouseCoordinate_t *)&BasePacket[1];
        auto Share = (Channel_ScreenShare_Share_t *)cc->m_StateData;

        Protocol_C2S_t::Channel_ScreenShare_Share_InformationToViewMouseCoordinate_t dt;
        dt.ChannelID = cc->m_ChannelID;
        dt.ChannelSessionID = cc->m_ChannelSessionID;
        dt.pos = CommandData->Position;
        TCP_WriteCommand(
          0,
          Protocol_C2S_t::Channel_ScreenShare_Share_InformationToViewMouseCoordinate,
          dt);

        break;
      }
      case ITC_Protocol_t::Channel_ScreenShare_View_MouseCoordinate:{
        auto CommandData = (ITC_Protocol_t::Channel_ScreenShare_View_MouseCoordinate_t *)&BasePacket[1];
        auto View = (Channel_ScreenShare_View_t *)cc->m_StateData;
        if(!(View->m_ChannelFlag & ProtocolChannel::ScreenShare::ChannelFlag::InputControl)){
          break;
        }
        Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostMouseCoordinate_t dt;
        dt.ChannelID = cc->m_ChannelID;
        dt.ChannelSessionID = cc->m_ChannelSessionID;
        dt.pos = CommandData->Position;
        TCP_WriteCommand(
          0,
          Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostMouseCoordinate,
          dt);
        break;
      }
      case ITC_Protocol_t::Channel_ScreenShare_View_MouseMotion:{
        auto CommandData = (ITC_Protocol_t::Channel_ScreenShare_View_MouseMotion_t *)&BasePacket[1];
        auto View = (Channel_ScreenShare_View_t *)cc->m_StateData;
        if(!(View->m_ChannelFlag & ProtocolChannel::ScreenShare::ChannelFlag::InputControl)){
          break;
        }
        Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostMouseMotion_t dt;
        dt.ChannelID = cc->m_ChannelID;
        dt.ChannelSessionID = cc->m_ChannelSessionID;
        dt.Motion = CommandData->Motion;
        TCP_WriteCommand(
          0,
          Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostMouseMotion,
          dt);
        break;
      }
      case ITC_Protocol_t::Channel_ScreenShare_View_MouseButton:{
        auto CommandData = (ITC_Protocol_t::Channel_ScreenShare_View_MouseButton_t *)&BasePacket[1];
        auto View = (Channel_ScreenShare_View_t *)cc->m_StateData;
        if(!(View->m_ChannelFlag & ProtocolChannel::ScreenShare::ChannelFlag::InputControl)){
          break;
        }
        Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostMouseButton_t dt;
        dt.ChannelID = cc->m_ChannelID;
        dt.ChannelSessionID = cc->m_ChannelSessionID;
        dt.key = CommandData->key;
        dt.state = CommandData->state;
        dt.pos = CommandData->pos;
        TCP_WriteCommand(
          0,
          Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostMouseButton,
          dt);
        break;
      }
      case ITC_Protocol_t::Channel_ScreenShare_View_KeyboardKey:{
        auto CommandData = (ITC_Protocol_t::Channel_ScreenShare_View_KeyboardKey_t *)&BasePacket[1];
        auto View = (Channel_ScreenShare_View_t *)cc->m_StateData;
        if(!(View->m_ChannelFlag & ProtocolChannel::ScreenShare::ChannelFlag::InputControl)){
          break;
        }
        Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostKeyboard_t dt;
        dt.ChannelID = cc->m_ChannelID;
        dt.ChannelSessionID = cc->m_ChannelSessionID;
        dt.Scancode = CommandData->Scancode;
        dt.State = CommandData->State;
        TCP_WriteCommand(
          0,
          Protocol_C2S_t::Channel_ScreenShare_View_ApplyToHostKeyboard,
          dt);
        break;
      }
    }

    i += ITC_Protocol.NA(BasePacket->Command)->m_DSS;
  }
  g_pile->ITC.bvec.Current = 0;
  TH_unlock(&g_pile->ITC.Mutex);
}

void InitAndRun(){
  g_pile = new pile_t;

  EV_open(&g_pile->listener);

  TH_mutex_init(&g_pile->ITC.Mutex);
  pile_t::ITC_t::bvec_Open(&g_pile->ITC.bvec);
  EV_async_init(&g_pile->listener, &g_pile->ITC.ev_async, ITC_read_cb);
  EV_async_start(&g_pile->listener, &g_pile->ITC.ev_async);

  EV_event_t evio_stdin;
  IO_fd_t fd_stdin;
  IO_fd_set(&fd_stdin, FD_IN);
  EV_event_init_fd(&evio_stdin, &fd_stdin, evio_stdin_cb, EV_READ);
  EV_event_start(&g_pile->listener, &evio_stdin);

  g_pile->TCP.tcp = NET_TCP_alloc(&g_pile->listener);

  g_pile->TCP.extid = NET_TCP_EXT_new(g_pile->TCP.tcp, sizeof(TCPMain_SockData_t), sizeof(TCPMain_PeerData_t));
  TCPMain_SockData_t *SockData = (TCPMain_SockData_t *)NET_TCP_GetSockData(g_pile->TCP.tcp, g_pile->TCP.extid);

  NET_TCP_layer_state_open(g_pile->TCP.tcp, g_pile->TCP.extid, (NET_TCP_cb_state_t)TCPMain_state_cb);
  SockData->ReadLayerID = NET_TCP_layer_read_open(g_pile->TCP.tcp, g_pile->TCP.extid, (NET_TCP_cb_read_t)TCPMain_read_cb, A_resize, 0, 0);

  IDMap_Open(&g_pile->TCP.IDMap);
  NET_TCP_open(g_pile->TCP.tcp);

  if(NET_socket2(NET_AF_INET, NET_SOCK_DGRAM | NET_SOCK_NONBLOCK, NET_IPPROTO_UDP, &g_pile->UDP.udp) < 0){
    WriteInformation("udp socket rip\r\n");
    __abort();
  }

  EV_event_init_socket(&g_pile->UDP.ev_udp, &g_pile->UDP.udp, ev_udp_read_cb, EV_READ);
  EV_event_start(&g_pile->listener, &g_pile->UDP.ev_udp);

  ChannelMap_Open(&g_pile->ChannelMap);

  EV_start(&g_pile->listener);
}

int main(){
  InitAndRun();

  return 0;
}

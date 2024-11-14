bool IsChannelInvalid(Protocol_ChannelID_t ID){
  if(g_pile->ChannelList.inri(ID)){
    return true;
  }
  if(g_pile->ChannelList.IsNRSentinel(ID)){
    return true;
  }
  if(g_pile->ChannelList.IsNodeReferenceRecycled(ID)){
    return true;
  }
  return false;
}
bool IsChannelSessionInvalid(Protocol_ChannelID_t ChannelID, Protocol_ChannelSessionID_t ChannelSessionID){
  auto Channel = &g_pile->ChannelList[ChannelID];

  if(Channel->SessionList.inri(ChannelSessionID)){
    return true;
  }
  if(Channel->SessionList.IsNRSentinel(ChannelSessionID)){
    return true;
  }
  if(Channel->SessionList.IsNodeReferenceRecycled(ChannelSessionID)){
    return true;
  }
  return false;
}
bool
IsAnythingInvalid(
  Protocol_SessionID_t SessionID,
  Protocol_ChannelID_t ChannelID,
  Protocol_ChannelSessionID_t ChannelSessionID
){
  if(IsChannelInvalid(ChannelID) == true){
    return true;
  }
  if(IsChannelSessionInvalid(ChannelID, ChannelSessionID) == true){
    return true;
  }
  auto Channel = &g_pile->ChannelList[ChannelID];
  auto ChannelSession = &Channel->SessionList[ChannelSessionID];
  if(ChannelSession->SessionID != SessionID){
    return true;
  }
  return false;
}

ChannelList_NodeReference_t AddChannel_ScreenShare(Protocol_SessionID_t HostSessionID){
  auto cnr = g_pile->ChannelList.NewNodeLast();
  auto cn = g_pile->ChannelList.GetNodeByReference(cnr);

  cn->data.Type = Protocol::ChannelType_ScreenShare_e;
  cn->data.SessionList.Open();
  cn->data.Buffer = A_resize(0, sizeof(Channel_ScreenShare_Data_t));

  auto data = (Channel_ScreenShare_Data_t *)cn->data.Buffer;
  data->Flag = 0;
  data->HostSessionID = HostSessionID;

  return cnr;
}

void
Channel_KickSession(
  Protocol_ChannelID_t ChannelID,
  Protocol_ChannelSessionID_t ChannelSessionID,
  Protocol::KickedFromChannel_Reason_t Reason = Protocol::KickedFromChannel_Reason_t::Unknown
){
  auto Channel = &g_pile->ChannelList[ChannelID];
  auto ChannelSession = &Channel->SessionList[ChannelSessionID];
  auto Session = &g_pile->SessionList[ChannelSession->SessionID];

  Protocol_S2C_t::KickedFromChannel_t rest;
  rest.ChannelID = ChannelID;
  rest.Reason = Reason;
  TCP_WriteCommand(
    Session->TCP.peer,
    0,
    Protocol_S2C_t().KickedFromChannel,
    rest);

  Session->ChannelList.unlrec(ChannelSession->SessionChannelID);
  Channel->SessionList.unlrec(ChannelSessionID);
}

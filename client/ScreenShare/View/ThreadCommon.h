ChannelInfo_t ChannelInfo;

struct : TRT_CON0_t<ThreadDecode_t, 1>{EV_tp_t tp;}ThreadDecode;
struct : TRT_CON0_t<ThreadWindow_t>{EV_tp_t tp;}ThreadWindow;
struct : TRT_CON0_t<Channel_ScreenShare_View_t>{}View;
struct : TRT_CON0_Itself_t<ThreadCommon_t, 2>{}Itself;

static bool ThreadDecode_tp_outside_cb(EV_t *listener, EV_tp_t *tp){
  auto ThreadCommon = OFFSETLESS(tp, ThreadCommon_t, ThreadDecode.tp);

  auto This = ThreadCommon->ThreadDecode.GetOrphanPointer();

  while(1){
    This->FramePacketList_Mutex.Lock();
    auto fpnr = This->FramePacketList.GetNodeFirst();
    auto fp = &This->FramePacketList[fpnr];
    uint32_t Size = fp->Size;
    uint8_t *Data = fp->Data;
    #if set_VerboseProtocol_HoldStreamTimes == 1
      uint64_t t = T_nowi();
      WriteInformation("[CLIENT] [DEBUG] [DECODER] VerboseTime nr:%lx time:%llu\r\n", fpnr, t - fp->_VerboseTime);
      fp->_VerboseTime = t;
    #endif
    This->FramePacketList_Mutex.Unlock();

    This->Decoder_Mutex.Lock();

    if(This->DecoderUserProperties.Updated == true){
      do{
        auto Window = ThreadCommon->ThreadWindow.Mark();
        if(Window == NULL){
          break;
        }
        Window->NewReadMethod(ETC_VEDC_Decoder_ReadType_Unknown);
        EV_async_send(&Window->ev, &Window->ev_async);
        ThreadCommon->ThreadWindow.Unmark();
      }while(0);

      This->DecoderUserProperties.Mutex.Lock();
      This->DecoderUserProperties.Updated = false;
      ETC_VEDC_Decoder_Close(&This->Decoder);
      auto r = ETC_VEDC_Decoder_Open(
        &This->Decoder,
        This->DecoderUserProperties.DecoderNameSize,
        This->DecoderUserProperties.DecoderName,
        0);
      if(r != ETC_VEDC_Decoder_Error_OK){

        std::string_view decoder_name = std::string_view((const char*)This->DecoderUserProperties.DecoderName, This->DecoderUserProperties.DecoderNameSize);

        fan::printn8(
          "[CLIENT] [WARNING] [DECODER] ", __FUNCTION__, " ", __FILE__, ":", __LINE__,
          " (ETC_VEDC_Decoder_Open returned (", r, ") for encoder \"",
          decoder_name, "\""
        );

        fan::printn8(
          "[CLIENT] [WARNING] [DECODER] ", __FUNCTION__, " ", __FILE__, ":", __LINE__,
          " falling back to OpenH264 decoder."
        );

       /* WriteInformation(
          "[CLIENT] [WARNING] [DECODER] %s %s:%lu ETC_VEDC_Decoder_Open returned (%lx) for encoder \"%.*s\"\r\n",
          __FUNCTION__, __FILE__, __LINE__,
          r, This->DecoderUserProperties.DecoderNameSize, This->DecoderUserProperties.DecoderName);
        WriteInformation(
          "[CLIENT] [WARNING] [DECODER] %s %s:%lu falling back to OpenH264 decoder.\r\n",
          __FUNCTION__, __FILE__, __LINE__);*/

        const char *CodecName = "OpenH264";
        if(ETC_VEDC_Decoder_Open(&This->Decoder, MEM_cstrlen(CodecName), CodecName, 0) != ETC_VEDC_Decoder_Error_OK){
          WriteInformation(
            "[CLIENT] [WARNING] %s %s:%lu failed to open OpenH264 decoder. going for OpenNothing.\r\n",
            __FUNCTION__, __FILE__, __LINE__);
          ETC_VEDC_Decoder_OpenNothing(&This->Decoder);
        }
      }
      This->DecoderUserProperties.Mutex.Unlock();
    }

    sintptr_t r = ETC_VEDC_Decoder_Write(&This->Decoder, Data, Size);

    This->Decoder_Mutex.Unlock();

    if(r != Size){
      WriteInformation(
        "[CLIENT] [WARNING] %s %s:%lu ETC_VEDC_Decoder_Write didnt return Size\r\n",
        __FUNCTION__, __FILE__, __LINE__);
      goto gt_NoFrame;
    }
    {
      auto Window = ThreadCommon->ThreadWindow.Mark();
      if(Window == NULL){
        goto gt_NoFrame;
      }
      EV_async_send(&Window->ev, &Window->ev_async);
      ThreadCommon->ThreadWindow.Unmark();
    }

    gt_NoFrame:

    #if set_VerboseProtocol_HoldStreamTimes == 1
      t = T_nowi();
      WriteInformation("[CLIENT] [DEBUG] [DECODER] VerboseTime DecodeEnd time:%llu\r\n", t - fp->_VerboseTime);
      fp->_VerboseTime = t;
    #endif

    A_resize(Data, 0);

    This->FramePacketList_Mutex.Lock();
    This->FramePacketList.unlrec(fpnr);
    if(This->FramePacketList.Usage() == 0){
      This->FramePacketList_Mutex.Unlock();
      ThreadCommon->Itself.Decrease();
      return 0;
    }
    This->FramePacketList_Mutex.Unlock();

    if(ThreadCommon->View.IsMarkable() == false){
      ThreadCommon->Itself.Decrease();
      return 0;
    }
  }
}
static void ThreadDecode_tp_inside_cb(EV_t *listener, EV_tp_t *tp, sint32_t err){

}

static bool ThreadWindow_tp_outside_cb(EV_t *listener, EV_tp_t *tp){
  auto ThreadCommon = OFFSETLESS(tp, ThreadCommon_t, ThreadWindow.tp);

  auto This = new ThreadWindow_t;

  This->ThreadCommon = ThreadCommon;

  EV_open(&This->ev);
  EV_async_init(&This->ev, &This->ev_async, [](EV_t *listener, EV_async_t *async){
    auto This = OFFSETLESS(listener, ThreadWindow_t, ev);
    auto ThreadDecode = This->ThreadCommon->ThreadDecode.GetOrphanPointer();

    ThreadDecode->Decoder_Mutex.Lock();

    bool IsReadable = ETC_VEDC_Decoder_IsReadable(&ThreadDecode->Decoder);
    if(IsReadable == false){
      goto gt_End;
    }

    if(0){}
    #ifdef __GPU_CUDA
      else if(ETC_VEDC_Decoder_IsReadType(&ThreadDecode->Decoder, ETC_VEDC_Decoder_ReadType_CudaArrayFrame)){
        This->NewReadMethod(ETC_VEDC_Decoder_ReadType_CudaArrayFrame);

        CUcontext CudaContext = (CUcontext)ETC_VEDC_Decoder_GetUnique(
          &ThreadDecode->Decoder,
          ETC_VEDC_Decoder_UniqueType_CudaContext);
        if(cuCtxSetCurrent(CudaContext) != CUDA_SUCCESS){
          __abort();
        }

        ETC_VEDC_Decoder_ImageProperties_t ImageProperties;
        ETC_VEDC_Decoder_GetReadImageProperties(
          &ThreadDecode->Decoder,
          ETC_VEDC_Decoder_ReadType_CudaArrayFrame,
          &ImageProperties);

        /* TODO hardcoded pixel format because fan uses different pixel format lib */
        This->ReadMethodData.CudaArrayFrame.cuda_textures.resize(
          &This->loco,
          This->FrameCID,
          fan::graphics::image_format::nv12,
          fan::vec2ui(ImageProperties.SizeX, ImageProperties.SizeY));

        ETC_VEDC_Decoder_CudaArrayFrame_t Frame;
        for(uint32_t i = 0; i < 4; i++){
          Frame.Array[i] = This->ReadMethodData.CudaArrayFrame.cuda_textures.get_array(i);
        }

        if(ETC_VEDC_Decoder_Read(
          &ThreadDecode->Decoder,
          ETC_VEDC_Decoder_ReadType_CudaArrayFrame,
          &Frame
        ) != ETC_VEDC_Decoder_Error_OK){
          goto gt_End;
        }

        This->FrameSize = fan::vec2ui(ImageProperties.SizeX, ImageProperties.SizeY);

        ETC_VEDC_Decoder_ReadClear(&ThreadDecode->Decoder, ETC_VEDC_Decoder_ReadType_CudaArrayFrame, &Frame);
      }
    #endif
    else if(ETC_VEDC_Decoder_IsReadType(&ThreadDecode->Decoder, ETC_VEDC_Decoder_ReadType_Frame)){
      This->NewReadMethod(ETC_VEDC_Decoder_ReadType_Frame);

      ETC_VEDC_Decoder_Frame_t Frame;
      if(ETC_VEDC_Decoder_Read(
        &ThreadDecode->Decoder,
        ETC_VEDC_Decoder_ReadType_Frame,
        &Frame
      ) != ETC_VEDC_Decoder_Error_OK){
        goto gt_End;
      }

      if(Frame.Properties.Stride[0] != Frame.Properties.Stride[1] * 2){
        WriteInformation(
          "[CLIENT] [WARNING] %s %s:%u fan doesnt support strides %lu %lu\r\n",
          __FUNCTION__, __FILE__, __LINE__, Frame.Properties.Stride[0], Frame.Properties.Stride[1]);
        __abort();
      }

      {
        uint32_t pixel_format;
        switch(Frame.Properties.PixelFormat){
          case PIXF_YUV420p:{pixel_format = fan::graphics::image_format::yuv420p; break;}
          case PIXF_YUVNV12:{pixel_format = fan::graphics::image_format::nv12; break;}
          default:{
            /* not supported pixel format... at least yet.*/
            __abort();
          }
        }
        f32_t sx = (f32_t)Frame.Properties.SizeX / Frame.Properties.Stride[0];
        This->FrameCID.set_tc_size(fan::vec2(sx, 1));
        This->FrameCID.reload(
          pixel_format,
          (void **)Frame.Data,
          fan::vec2ui(Frame.Properties.Stride[0], Frame.Properties.SizeY));
      }

      This->FrameSize = fan::vec2ui(Frame.Properties.SizeX, Frame.Properties.SizeY);

      ETC_VEDC_Decoder_ReadClear(&ThreadDecode->Decoder, ETC_VEDC_Decoder_ReadType_Frame, &Frame);
    }
    else{
      WriteInformation(
        "[CLIENT] [WARNING] %s %s:%u decoder doesnt support any readtype ecps can use\r\n",
        __FUNCTION__, __FILE__, __LINE__);
    }

    gt_End:
    ThreadDecode->Decoder_Mutex.Unlock();

    {
      auto View = This->ThreadCommon->View.Mark();
      if(View == NULL){
        __abort();
      }
      auto ChannelFlag = View->m_ChannelFlag; /* TODO please be atomic */
      This->ThreadCommon->View.Unmark();
      This->m_ChannelFlag = ChannelFlag;
    }

    {
      uint32_t we = This->loco.window.handle_events();
      if (This->loco.should_close()) {
        __abort();
      }
      This->HandleCursor();
      This->loco.process_loop();
    }
  });
  EV_async_start(&This->ev, &This->ev_async);

  TH_mutex_init(&This->HostMouseCoordinate.Mutex);
  This->HostMouseCoordinate.had = 0;
  This->HostMouseCoordinate.got = 0;

  This->viewport = gloco->open_viewport(
    0,
    This->loco.window.get_size());
  This->loco.window.add_resize_callback(
    [](const fan::window_t::resize_cb_data_t &p) {
      loco_t *loco = gloco.loco;
      auto This = OFFSETLESS(loco, ThreadWindow_t, loco);
      gloco->viewport_set(This->viewport, 0, p.size, p.window->get_size());
      fan::vec2 CursorSize = fan::vec2(16) / p.window->get_size();
      This->CursorCID.set_size(CursorSize);
    }
  );
  This->camera = This->loco.open_camera(fan::vec2(-1, +1), fan::vec2(-1, +1));
  This->TexturePack.open_compiled("tpack");
  {
    bool r = This->TexturePack.qti("cursor", &This->TextureCursor);
    if(r != 0){
      __abort();
    }
  }
  This->FrameRenderSize = 1;
  This->OpenFrameAndCursor();

  This->KeyboardKeyCallbackID = This->loco.window.add_keys_callback(ThreadWindow_t::Keys_cb);
  This->MouseButtonCallbackID = This->loco.window.add_buttons_callback(ThreadWindow_t::MouseButtons_cb);

  /* everything is ready now */
  ThreadCommon->ThreadWindow.SetPointer(This);

  EV_start(&This->ev);

  __abort();

  return 0;
}
static void ThreadWindow_tp_inside_cb(EV_t *listener, EV_tp_t *tp, sint32_t err){

}

void StartDecoder(){
  Itself.Increase();
  EV_tp_init(&ThreadDecode.tp, ThreadDecode_tp_outside_cb, ThreadDecode_tp_inside_cb, 0);
  EV_tp_start(&g_pile->listener, &ThreadDecode.tp);
}

ThreadCommon_t(Channel_ScreenShare_View_t *p_View){
  ChannelInfo = p_View->ChannelInfo;

  View.LocklessSetPointer(p_View);

  ThreadDecode.LocklessSetPointer(new ThreadDecode_t);

  EV_tp_init(&ThreadWindow.tp, ThreadWindow_tp_outside_cb, ThreadWindow_tp_inside_cb, 1);
  EV_tp_start(&g_pile->listener, &ThreadWindow.tp);
}
~ThreadCommon_t(){
  {
    auto Decode = ThreadDecode.GetOrphanPointer();
    if(Decode != NULL){
      __abort();
    }
  }
}

#pragma once

// name, stable module-local id, A32 word parameter count, visibility, method
#define OGPLAY_OPENSLES_BOUNDARY_EXPORTS(X)                                      \
    X("slCreateEngine", 0, 6, public_function, CreateEngine)                   \
    X("slQueryNumSupportedEngineInterfaces", 1, 1, public_function,            \
      QueryNumEngineInterfaces)                                                 \
    X("slQuerySupportedEngineInterfaces", 2, 2, public_function,               \
      QueryEngineInterface)                                                     \
    X("$Object.Realize", 10, 2, private_callable, ObjectRealize)               \
    X("$Object.Resume", 11, 2, private_callable, ObjectResume)                 \
    X("$Object.GetState", 12, 2, private_callable, ObjectGetState)             \
    X("$Object.GetInterface", 13, 3, private_callable, ObjectGetInterface)     \
    X("$Object.RegisterCallback", 14, 3, private_callable,                     \
      ObjectRegisterCallback)                                                   \
    X("$Object.AbortAsyncOperation", 15, 1, private_callable,                  \
      ObjectAbortAsync)                                                         \
    X("$Object.Destroy", 16, 1, private_callable, ObjectDestroy)               \
    X("$Object.SetPriority", 17, 3, private_callable, ObjectSetPriority)       \
    X("$Object.GetPriority", 18, 3, private_callable, ObjectGetPriority)       \
    X("$Object.SetLossOfControlInterfaces", 19, 4, private_callable,           \
      ObjectSetLossOfControl)                                                   \
    X("$Engine.CreateLEDDevice", 30, 6, private_callable, EngineCreateLED)     \
    X("$Engine.CreateVibraDevice", 31, 6, private_callable, EngineCreateVibra) \
    X("$Engine.CreateAudioPlayer", 32, 7, private_callable,                    \
      EngineCreateAudioPlayer)                                                  \
    X("$Engine.CreateAudioRecorder", 33, 7, private_callable,                 \
      EngineCreateAudioRecorder)                                               \
    X("$Engine.CreateMidiPlayer", 34, 11, private_callable, EngineCreateMidi) \
    X("$Engine.CreateListener", 35, 5, private_callable, EngineCreateListener)\
    X("$Engine.Create3DGroup", 36, 5, private_callable, EngineCreate3DGroup)  \
    X("$Engine.CreateOutputMix", 37, 5, private_callable,                     \
      EngineCreateOutputMix)                                                    \
    X("$Engine.CreateMetadataExtractor", 38, 6, private_callable,             \
      EngineCreateMetadataExtractor)                                           \
    X("$Engine.CreateExtensionObject", 39, 8, private_callable,               \
      EngineCreateExtensionObject)                                             \
    X("$Engine.QueryNumSupportedInterfaces", 40, 3, private_callable,          \
      EngineQueryNumInterfaces)                                                 \
    X("$Engine.QuerySupportedInterfaces", 41, 4, private_callable,             \
      EngineQueryInterface)                                                     \
    X("$Engine.QueryNumSupportedExtensions", 42, 2, private_callable,          \
      EngineQueryNumExtensions)                                                 \
    X("$Engine.QuerySupportedExtension", 43, 4, private_callable,             \
      EngineQueryExtension)                                                     \
    X("$Engine.IsExtensionSupported", 44, 3, private_callable,                 \
      EngineIsExtensionSupported)                                               \
    X("$OutputMix.GetDestinationOutputDeviceIDs", 50, 3, private_callable,     \
      OutputMixGetDevices)                                                      \
    X("$OutputMix.RegisterDeviceChangeCallback", 51, 3, private_callable,      \
      OutputMixRegisterCallback)                                                \
    X("$OutputMix.ReRoute", 52, 3, private_callable, OutputMixReRoute)         \
    X("$Play.SetPlayState", 60, 2, private_callable, PlaySetState)             \
    X("$Play.GetPlayState", 61, 2, private_callable, PlayGetState)             \
    X("$Play.GetDuration", 62, 2, private_callable, PlayGetDuration)           \
    X("$Play.GetPosition", 63, 2, private_callable, PlayGetPosition)           \
    X("$Play.RegisterCallback", 64, 3, private_callable, PlayRegisterCallback)\
    X("$Play.SetCallbackEventsMask", 65, 2, private_callable, PlaySetMask)     \
    X("$Play.GetCallbackEventsMask", 66, 2, private_callable, PlayGetMask)     \
    X("$Play.SetMarkerPosition", 67, 2, private_callable, PlaySetMarker)       \
    X("$Play.ClearMarkerPosition", 68, 1, private_callable, PlayClearMarker)   \
    X("$Play.GetMarkerPosition", 69, 2, private_callable, PlayGetMarker)       \
    X("$Play.SetPositionUpdatePeriod", 70, 2, private_callable,                \
      PlaySetUpdatePeriod)                                                      \
    X("$Play.GetPositionUpdatePeriod", 71, 2, private_callable,                \
      PlayGetUpdatePeriod)                                                      \
    X("$BufferQueue.Enqueue", 80, 3, private_callable, BufferQueueEnqueue)     \
    X("$BufferQueue.Clear", 81, 1, private_callable, BufferQueueClear)         \
    X("$BufferQueue.GetState", 82, 2, private_callable, BufferQueueGetState)   \
    X("$BufferQueue.RegisterCallback", 83, 3, private_callable,                \
      BufferQueueRegisterCallback)                                              \
    X("$Volume.SetVolumeLevel", 90, 2, private_callable, VolumeSetLevel)       \
    X("$Volume.GetVolumeLevel", 91, 2, private_callable, VolumeGetLevel)       \
    X("$Volume.GetMaxVolumeLevel", 92, 2, private_callable, VolumeGetMaxLevel)\
    X("$Volume.SetMute", 93, 2, private_callable, VolumeSetMute)               \
    X("$Volume.GetMute", 94, 2, private_callable, VolumeGetMute)               \
    X("$Volume.EnableStereoPosition", 95, 2, private_callable,                 \
      VolumeEnableStereo)                                                       \
    X("$Volume.IsEnabledStereoPosition", 96, 2, private_callable,              \
      VolumeIsStereoEnabled)                                                    \
    X("$Volume.SetStereoPosition", 97, 2, private_callable, VolumeSetStereo)   \
    X("$Volume.GetStereoPosition", 98, 2, private_callable, VolumeGetStereo)

#pragma once
#include <cstdint>
using aaudio_result_t=int32_t;
using aaudio_data_callback_result_t=int32_t;
struct AAudioStream;
using DataCallback=aaudio_data_callback_result_t(*)(AAudioStream*,void*,void*,int32_t);
using ErrorCallback=void(*)(AAudioStream*,void*,aaudio_result_t);
struct AAudioStreamBuilder { DataCallback data{}; ErrorCallback error{}; void* user{}; };
struct AAudioStream { DataCallback data{}; ErrorCallback error{}; void* user{}; };
constexpr int AAUDIO_OK=0,AAUDIO_CALLBACK_RESULT_CONTINUE=0,AAUDIO_DIRECTION_OUTPUT=0,
AAUDIO_FORMAT_PCM_I16=1,AAUDIO_SHARING_MODE_SHARED=1,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY=12;
inline int AAudio_createStreamBuilder(AAudioStreamBuilder** b) { *b=new AAudioStreamBuilder;return 0; }
inline void AAudioStreamBuilder_setDirection(AAudioStreamBuilder*,int){}
inline void AAudioStreamBuilder_setFormat(AAudioStreamBuilder*,int){}
inline void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder*,int){}
inline void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder*,int){}
inline void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder*,int){}
inline void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder*,int){}
inline void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder*,int){}
inline void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder*b,DataCallback d,void*u) { b->data=d;b->user=u; }
inline void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder*b,ErrorCallback e,void*) { b->error=e; }
inline int AAudioStreamBuilder_openStream(AAudioStreamBuilder*b,AAudioStream**s) { *s=new AAudioStream{b->data,b->error,b->user};return 0; }
inline void AAudioStreamBuilder_delete(AAudioStreamBuilder*b) { delete b; }
inline int AAudioStream_getFramesPerBurst(AAudioStream*) { return 192; }
inline int AAudioStream_setBufferSizeInFrames(AAudioStream*,int n) { return n; }
inline int AAudioStream_requestStart(AAudioStream*) { return 0; }
inline int AAudioStream_requestStop(AAudioStream*) { return 0; }
inline int AAudioStream_close(AAudioStream*s) { delete s;return 0; }
inline int AAudioStream_getXRunCount(AAudioStream*) { return 0; }

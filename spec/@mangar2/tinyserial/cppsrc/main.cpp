#include <napi.h>
#include "Samples/classexample.h"
#include "tinyserial/tinyserial.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  ClassExample::Init(env, exports);
  return TinySerial::Init(env, exports);
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, InitAll)
#include <napi.h>
#include "tinyserialimpl.h"

class TinySerial : public Napi::ObjectWrap<TinySerial> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    TinySerial(const Napi::CallbackInfo& info);
    TinySerialImpl* GetInternalInstance();

private:
    static Napi::FunctionReference constructor;
    Napi::Value open(const Napi::CallbackInfo& info);
    Napi::Value write(const Napi::CallbackInfo& info);
    Napi::Value read(const Napi::CallbackInfo& info);
    Napi::Value close(const Napi::CallbackInfo& info);
    Napi::Value setOptions(const Napi::CallbackInfo& info);
    TinySerialImpl *_tinySerialImpl;
};
#include "tinyserial.h"

Napi::FunctionReference TinySerial::constructor;

Napi::Object TinySerial::Init(Napi::Env env, Napi::Object exports) {
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "TinySerial", {
        InstanceMethod("open", &TinySerial::open),
        InstanceMethod("setOptions", &TinySerial::setOptions),
        InstanceMethod("write", &TinySerial::write),
        InstanceMethod("read", &TinySerial::read),
        InstanceMethod("close", &TinySerial::close)

    });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("TinySerial", func);
    return exports;
}

TinySerial::TinySerial(const Napi::CallbackInfo& info) : Napi::ObjectWrap<TinySerial>(info)  {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    int length = info.Length();
    
    if (length != 0) {
        Napi::TypeError::New(env, "No argument expected").ThrowAsJavaScriptException();
    }

    this->_tinySerialImpl = new TinySerialImpl();
}

Napi::Value TinySerial::open(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String Expected").ThrowAsJavaScriptException();
    }

    Napi::String comPortName = info[0].As<Napi::String>();
    bool answer = this->_tinySerialImpl->open(comPortName);

    return Napi::Boolean::New(info.Env(), answer);
}

Napi::Value TinySerial::setOptions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1) {
        Napi::TypeError::New(env, "One parameter expected").ThrowAsJavaScriptException();
    }

    Napi::Object options = info[0].As<Napi::Object>();
    uint32_t baudRate = options.Has("baudRate") ? options.Get("baudRate").ToNumber() : CBR_9600;
    uint8_t dataBits = options.Has("dataBits") ? options.Get("dataBits").ToNumber() : 8;
    uint8_t stopBits = options.Has("stopBits") ? options.Get("stopBits").ToNumber() : ONESTOPBIT;
    uint8_t parity = options.Has("parity") ? options.Get("parity").ToNumber() : NOPARITY;
    bool answer = this->_tinySerialImpl->setOptions(baudRate, dataBits, stopBits, parity);

    return Napi::Boolean::New(info.Env(), answer);
}

Napi::Value TinySerial::write(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String Expected").ThrowAsJavaScriptException();
    }

    Napi::String data = info[0].As<Napi::String>();
    uint32_t bytesWritten = this->_tinySerialImpl->write(data);

    return Napi::Number::New(info.Env(), bytesWritten);
}

Napi::Value TinySerial::read(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);
    BUFFER_SIZE = 1024;
    const char buffer[BUFFER_SIZE + 1];
    uint32_t bytesRead = this->_tinySerialImpl->read(buffer, bufferSize);

    return Napi::Number::New(info.Env(), bytesRead);
}

Napi::Value TinySerial::close(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() != 0) {
        Napi::TypeError::New(env, "Close has no parameter").ThrowAsJavaScriptException();
    }

    bool answer = this->_tinySerialImpl->close();
    return Napi::Boolean::New(info.Env(), answer);
}


TinySerialImpl* TinySerial::GetInternalInstance() {
    return this->_tinySerialImpl;
}
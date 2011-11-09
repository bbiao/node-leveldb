#include "DB.h"
#include "Iterator.h"
#include "WriteBatch.h"

#include <node_buffer.h>
#include "helpers.h"

using namespace node_leveldb;

Persistent<FunctionTemplate> DB::persistent_function_template;

DB::DB() 
  : db(NULL)
{
}

DB::~DB() {
  if (db != NULL) {
    delete db;
    db = NULL;
  }
}

void DB::Init(Handle<Object> target) {
  HandleScope scope; // used by v8 for garbage collection

  // Our constructor
  Local<FunctionTemplate> local_function_template = FunctionTemplate::New(New);
  persistent_function_template = Persistent<FunctionTemplate>::New(local_function_template);
  persistent_function_template->InstanceTemplate()->SetInternalFieldCount(1);
  persistent_function_template->SetClassName(String::NewSymbol("DB"));

  // Instance methods
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "open", Open);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "close", Close);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "put", Put);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "del", Del);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "write", Write);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "get", Get);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "newIterator", NewIterator);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "getSnapshot", GetSnapshot);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "releaseSnapshot", ReleaseSnapshot);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "getProperty", GetProperty);
  NODE_SET_PROTOTYPE_METHOD(persistent_function_template, "getApproximateSizes", GetApproximateSizes);

  // Static methods
  NODE_SET_METHOD(persistent_function_template, "destroyDB", DestroyDB);
  NODE_SET_METHOD(persistent_function_template, "repairDB", RepairDB);

  // Binding our constructor function to the target variable
  target->Set(String::NewSymbol("DB"), persistent_function_template->GetFunction());
}

Handle<Value> DB::New(const Arguments& args) {
  HandleScope scope;

  DB* self = new DB();
  self->Wrap(args.This());

  return args.This();
}


//
// Open
//

Handle<Value> DB::Open(const Arguments& args) {
  HandleScope scope;

  // Get this and arguments
  DB* self = ObjectWrap::Unwrap<DB>(args.This());

  // Required filename
  if (args.Length() < 1 || !args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New("DB.open() expects a filename")));
  }
  String::Utf8Value name(args[0]);
  
  int pos = 1;

  // Optional options
  leveldb::Options options = leveldb::Options();
  if (pos < args.Length() && args[pos]->IsObject() && !args[pos]->IsFunction()) {
    options = JsToOptions(args[pos]);
    pos++;
  }

  // Optional callback
  Local<Function> callback;
  if (pos < args.Length() && args[pos]->IsFunction()) {
    callback = Local<Function>::Cast(args[pos]);
    pos++;
  }

  // Pass parameters to async function
  OpenParams *params = new OpenParams(self, *name, options, callback);
  EIO_BeforeOpen(params);

  return args.This();
}

void DB::EIO_BeforeOpen(OpenParams *params) {
  eio_custom(EIO_Open, EIO_PRI_DEFAULT, EIO_AfterOpen, params);
}

eio_return_type DB::EIO_Open(eio_req *req) {
  OpenParams *params = static_cast<OpenParams*>(req->data);
  DB *self = params->self;

  // Close old DB, if open() is called more than once
  if (self->db != NULL) {
    delete self->db;
    self->db = NULL;
  }

  // Do the actual work
  params->status = leveldb::DB::Open(params->options, params->name, &self->db);

  eio_return_stmt;
}

int DB::EIO_AfterOpen(eio_req *req) {
  HandleScope scope;

  OpenParams *params = static_cast<OpenParams*>(req->data);
  params->Callback();
  
  delete params;
  return 0;
}


//
// Close
//

Handle<Value> DB::Close(const Arguments& args) {
  HandleScope scope;

  // Get this and arguments
  DB* self = ObjectWrap::Unwrap<DB>(args.This());
  
  // Optional callback
  Local<Function> callback;
  if (0 < args.Length() && args[0]->IsFunction()) {
    callback = Local<Function>::Cast(args[0]);
  }

  Params *params = new Params(self, callback);
  EIO_BeforeClose(params);
  
  return args.This();
}

void DB::EIO_BeforeClose(Params *params) {
  eio_custom(EIO_Close, EIO_PRI_DEFAULT, EIO_AfterClose, params);
}

eio_return_type DB::EIO_Close(eio_req *req) {
  Params *params = static_cast<Params*>(req->data);
  DB *self = params->self;

  if (self->db != NULL) {
    delete self->db;
    self->db = NULL;
  }
  
  eio_return_stmt;
}

int DB::EIO_AfterClose(eio_req *req) {
  Params *params = static_cast<Params*>(req->data);
  params->Callback();
  
  delete params;
  return 0;
}


//
// Put
//

Handle<Value> DB::Put(const Arguments& args) {
  HandleScope scope;
  
  // Get this and arguments
  DB* self = ObjectWrap::Unwrap<DB>(args.This());
  if (self->db == NULL) {
    return ThrowException(Exception::Error(String::New("DB has not been opened")));
  }
  
  // Check args
  if (args.Length() < 2 || (!args[0]->IsString() && !Buffer::HasInstance(args[0])) || (!args[1]->IsString() && !Buffer::HasInstance(args[1]))) {
    return ThrowException(Exception::TypeError(String::New("DB.put() expects key, value")));
  }

  // Use temporary WriteBatch to implement Put
  WriteBatch *writeBatch = new WriteBatch();
  leveldb::Slice key = JsToSlice(args[0], &writeBatch->strings);
  leveldb::Slice value = JsToSlice(args[1], &writeBatch->strings);
  writeBatch->wb.Put(key, value);

  int pos = 2;

  // Optional write options
  leveldb::WriteOptions options = leveldb::WriteOptions();
  if (pos < args.Length() && args[pos]->IsObject() && !args[pos]->IsFunction()) {
    options = JsToWriteOptions(args[pos]);
    pos++;
  }

  // Optional callback
  Local<Function> callback;
  if (pos < args.Length() && args[pos]->IsFunction()) {
    callback = Local<Function>::Cast(args[pos]);
    pos++;
  }
  
  WriteParams *params = new WriteParams(self, writeBatch, options, callback);
  params->disposeWriteBatch = true;
  EIO_BeforeWrite(params);

  return args.This();
}


//
// Del
//

Handle<Value> DB::Del(const Arguments& args) {
  HandleScope scope;
  
  // Get this and arguments
  DB* self = ObjectWrap::Unwrap<DB>(args.This());
  if (self->db == NULL) {
    return ThrowException(Exception::Error(String::New("DB has not been opened")));
  }
  
  // Check args
  if (args.Length() < 1 || (!args[0]->IsString() && !Buffer::HasInstance(args[0]))) {
    return ThrowException(Exception::TypeError(String::New("DB.del() expects key")));
  }

  // Use temporary WriteBatch to implement Del
  WriteBatch *writeBatch = new WriteBatch();
  leveldb::Slice key = JsToSlice(args[0], &writeBatch->strings);
  writeBatch->wb.Delete(key);
  
  int pos = 1;

  // Optional write options
  leveldb::WriteOptions options = leveldb::WriteOptions();
  if (pos < args.Length() && args[pos]->IsObject() && !args[pos]->IsFunction()) {
    options = JsToWriteOptions(args[pos]);
    pos++;
  }

  // Optional callback
  Local<Function> callback;
  if (pos < args.Length() && args[pos]->IsFunction()) {
    callback = Local<Function>::Cast(args[pos]);
    pos++;
  }
  
  WriteParams *params = new WriteParams(self, writeBatch, options, callback);
  params->disposeWriteBatch = true;
  EIO_BeforeWrite(params);

  return args.This();
}


//
// Write
//

Handle<Value> DB::Write(const Arguments& args) {
  HandleScope scope;

  // Get this and arguments
  DB* self = ObjectWrap::Unwrap<DB>(args.This());
  if (self->db == NULL) {
    return ThrowException(Exception::Error(String::New("DB has not been opened")));
  }
  
  // Required WriteBatch
  if (args.Length() < 1 || !args[0]->IsObject()) {
    return ThrowException(Exception::TypeError(String::New("DB.write() expects a WriteBatch object")));
  }
  Local<Object> writeBatchObject = Object::Cast(*args[0]);
  WriteBatch* writeBatch = ObjectWrap::Unwrap<WriteBatch>(writeBatchObject);
  
  int pos = 1;

  // Optional write options
  leveldb::WriteOptions options = leveldb::WriteOptions();
  if (pos < args.Length() && args[pos]->IsObject() && !args[pos]->IsFunction()) {
    options = JsToWriteOptions(args[pos]);
    pos++;
  }

  // Optional callback
  Local<Function> callback;
  if (pos < args.Length() && args[pos]->IsFunction()) {
    callback = Local<Function>::Cast(args[pos]);
    pos++;
  }
  
  // Pass parameters to async function
  WriteParams *params = new WriteParams(self, writeBatch, options, callback);
  EIO_BeforeWrite(params);

  return args.This();
}

void DB::EIO_BeforeWrite(WriteParams *params) {
  eio_custom(EIO_Write, EIO_PRI_DEFAULT, EIO_AfterWrite, params);
}

eio_return_type DB::EIO_Write(eio_req *req) {
  WriteParams *params = static_cast<WriteParams*>(req->data);
  DB *self = params->self;

  // Do the actual work
  if (self->db != NULL) {
    params->status = self->db->Write(params->options, &params->writeBatch->wb);
  }

  eio_return_stmt;
}

int DB::EIO_AfterWrite(eio_req *req) {
  HandleScope scope;
  
  WriteParams *params = static_cast<WriteParams*>(req->data);
  params->Callback();

  if (params->disposeWriteBatch) {
    delete params->writeBatch;
  }

  delete params;
  return 0;
}


//
// Get
//

Handle<Value> DB::Get(const Arguments& args) {
  HandleScope scope;

  // Check args
  if (args.Length() < 1 || (!args[0]->IsString() && !Buffer::HasInstance(args[0]))) {
    return ThrowException(Exception::TypeError(String::New("DB.get() expects key")));
  }

  DB* self = ObjectWrap::Unwrap<DB>(args.This());
  if (self->db == NULL) {
    return ThrowException(Exception::Error(String::New("DB has not been opened")));
  }
  
  std::vector<std::string> *strings = NULL;
  if (args[0]->IsString()) {
    strings = new std::vector<std::string>(1);
  }
  leveldb::Slice key = JsToSlice(args[0], strings);
  
  int pos = 1;

  // Optional read options
  leveldb::ReadOptions options = leveldb::ReadOptions();
  if (pos < args.Length() && args[pos]->IsObject() && !args[pos]->IsFunction()) {
    options = JsToReadOptions(args[pos]);
    pos++;
  }

  // Optional callback
  Local<Function> callback;
  if (pos < args.Length() && args[pos]->IsFunction()) {
    callback = Local<Function>::Cast(args[pos]);
    pos++;
  }
  
  // Pass parameters to async function
  ReadParams *params = new ReadParams(self, key, options, callback, strings);
  EIO_BeforeRead(params);

  return args.This();
}

void DB::EIO_BeforeRead(ReadParams *params) {
  eio_custom(EIO_Read, EIO_PRI_DEFAULT, EIO_AfterRead, params);
}

eio_return_type DB::EIO_Read(eio_req *req) {
  ReadParams *params = static_cast<ReadParams*>(req->data);
  DB *self = params->self;

  // Do the actual work
  if (self->db != NULL) {
    params->status = self->db->Get(params->options, params->key, &params->result);
  }

  eio_return_stmt;
}

int DB::EIO_AfterRead(eio_req *req) {
  HandleScope scope;
  
  ReadParams *params = static_cast<ReadParams*>(req->data);
  params->Callback(String::New(params->result.data(), params->result.length()));

  delete params;
  return 0;
}


Handle<Value> DB::NewIterator(const Arguments& args) {
  HandleScope scope;

  if (!(args.Length() == 1 && args[0]->IsObject())) {
      return ThrowException(Exception::TypeError(String::New("Invalid arguments: Expected (Object)")));
  } // if
  
  DB* self = ObjectWrap::Unwrap<DB>(args.This());
  leveldb::ReadOptions options = JsToReadOptions(args[0]);
  leveldb::Iterator* it = self->db->NewIterator(options);

  // DJO: Don't like writing code I don't understand, but I found this is how the node-gd library
  // converts an actual instance to it's binding representation
  // https://github.com/taggon/node-gd/blob/master/gd_bindings.cc#L79
  // Guess, I'll understand it soon...
  Local<Value> _arg_ = External::New(it);
  Persistent<Object> _it_(Iterator::persistent_function_template->GetFunction()->NewInstance(1, &_arg_));
  
  return scope.Close(_it_);
}

Handle<Value> DB::GetSnapshot(const Arguments& args) {
  HandleScope scope;
  return ThrowException(Exception::Error(String::New("TODO: IMPLEMENT ME")));
}

Handle<Value> DB::ReleaseSnapshot(const Arguments& args) {
  HandleScope scope;
  return ThrowException(Exception::Error(String::New("TODO: IMPLEMENT ME")));
}

Handle<Value> DB::GetProperty(const Arguments& args) {
  HandleScope scope;
  return ThrowException(Exception::Error(String::New("TODO: IMPLEMENT ME")));
}

Handle<Value> DB::GetApproximateSizes(const Arguments& args) {
  HandleScope scope;
  return ThrowException(Exception::Error(String::New("TODO: IMPLEMENT ME")));
}


//
// DestroyDB
//

Handle<Value> DB::DestroyDB(const Arguments& args) {
  HandleScope scope;

  // Check args
  if (!(args.Length() == 2 && args[0]->IsString() && args[1]->IsObject())) {
    return ThrowException(Exception::TypeError(String::New("Invalid arguments: Expected (String, Object)")));
  }

  String::Utf8Value name(args[0]);
  leveldb::Options options = JsToOptions(args[1]);

  return processStatus(leveldb::DestroyDB(*name, options));
}


//
// RepairDB
//

Handle<Value> DB::RepairDB(const Arguments& args) {
  HandleScope scope;

  // Check args
  if (!(args.Length() == 2 && args[0]->IsString() && args[1]->IsObject())) {
    return ThrowException(Exception::TypeError(String::New("Invalid arguments: Expected (String, Object)")));
  }

  String::Utf8Value name(args[0]);
  leveldb::Options options = JsToOptions(args[1]);

  return processStatus(leveldb::RepairDB(*name, options));
}


//
// Implementation of Params, which are passed from JS thread to EIO thread and back again
//

DB::Params::Params(DB* self, Handle<Function> cb)
  : self(self)
{
  self->Ref();
  ev_ref(EV_DEFAULT_UC);
  callback = Persistent<Function>::New(cb);
}

DB::Params::~Params() {
  self->Unref();
  ev_unref(EV_DEFAULT_UC);
  callback.Dispose();
}

void DB::Params::Callback(Handle<Value> result) {
  if (!callback.IsEmpty()) {
    TryCatch try_catch;
    if (status.ok()) {
      // no error, callback with no arguments, or result as second argument
      if (result.IsEmpty()) {
        callback->Call(self->handle_, 0, NULL);
      } else {
        Handle<Value> argv[] = { Null(), result };
        callback->Call(self->handle_, 2, argv);
      }
    } else if (status.IsNotFound()) {
      // not found, return (null, undef)
      Handle<Value> argv[] = { Null() };
      callback->Call(self->handle_, 1, argv);
    } else {
      // error, callback with first argument as error object
      Handle<String> message = String::New(status.ToString().c_str());
      Handle<Value> argv[] = { Exception::Error(message) };
      callback->Call(self->handle_, 1, argv);
    }
    if (try_catch.HasCaught()) {
        FatalException(try_catch);
    }
  }
}

DB::ReadParams::~ReadParams() {
  if (strings != NULL) {
    delete strings;
  }
}



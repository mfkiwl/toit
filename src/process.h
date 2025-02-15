// Copyright (C) 2018 Toitware ApS.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; version
// 2.1 only.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// The license can be found in the file `LICENSE` in the top level
// directory of this repository.

#pragma once

#include "heap.h"
#include "interpreter.h"
#include "linked.h"
#include "profiler.h"
#include "resource.h"
#include "snapshot_bundle.h"


namespace toit {

enum MessageType {
  MESSAGE_INVALID = 0,
  MESSAGE_OBJECT_NOTIFY = 1,
  MESSAGE_SYSTEM = 2,
};

class Message;
typedef LinkedFIFO<Message> MessageFIFO;

class Message : public MessageFIFO::Element {
 public:
  virtual ~Message() { }

  virtual MessageType message_type() const = 0;

  bool is_object_notify() const { return message_type() == MESSAGE_OBJECT_NOTIFY; }
  bool is_system() const { return message_type() == MESSAGE_SYSTEM; }
};

// Process is linked into two different linked lists, so we have to make
// use of the arbitrary N template argument to distinguish the two.
typedef LinkedList<Process, 1> ProcessListFromProcessGroup;
typedef LinkedFIFO<Process, 2> ProcessListFromScheduler;

class Process : public ProcessListFromProcessGroup::Element,
                public ProcessListFromScheduler::Element {
 public:
  enum Signal {
    KILL              = 1 << 0,
    PRINT_STACK_TRACE = 1 << 1,
    PREEMPT           = 1 << 2,
    WATCHDOG          = 1 << 3,
  };

  enum State {
    IDLE,
    SCHEDULED,
    RUNNING,

    SUSPENDED_IDLE,
    SUSPENDED_SCHEDULED,
    SUSPENDED_AWAITING_GC
  };

  static const char* StateName[];

  Process(Program* program, ProcessGroup* group, char** args, Block* initial_block);
#ifndef TOIT_FREERTOS
  Process(Program* program, ProcessGroup* group, SnapshotBundle bundle, char** args, Block* initial_block);
#endif
  Process(Program* program, ProcessGroup* group, Method method, const uint8* arguments_address, int arguments_length, Block* initial_block);
  ~Process();

  int id() const { return _id; }
  int next_task_id() { return _next_task_id++; }

  bool is_suspended() const { return _state == SUSPENDED_IDLE || _state == SUSPENDED_SCHEDULED; }

  // Returns whether this process is privileged (a system process).
  bool is_privileged();

  // Garbage collection operation for runtime objects.
  int scavenge() {
    int result = object_heap()->scavenge();
    _memory_usage = object_heap()->usage("object heap after gc");
    return result;
  }

  bool idle_since_scavenge() const { return _idle_since_scavenge; }
  void set_idle_since_scavenge(bool value) { _idle_since_scavenge = value; }

  bool has_finalizer(HeapObject* key, Object* lambda) {
    return object_heap()->has_finalizer(key, lambda);
  }
  bool add_finalizer(HeapObject* key, Object* lambda) {
    return object_heap()->add_finalizer(key, lambda);
  }
  bool add_vm_finalizer(HeapObject* key) {
    return object_heap()->add_vm_finalizer(key);
  }
  bool remove_finalizer(HeapObject* key) {
    return object_heap()->remove_finalizer(key);
  }

  Object* next_finalizer_to_run() {
    return object_heap()->next_finalizer_to_run();
  }

  Program* program() { return _program; }
  ProcessGroup* group() { return _group; }
  ObjectHeap* object_heap() { return &_object_heap; }
  Usage* usage() { return &_memory_usage; }
  Task* task() { return object_heap()->task(); }

  void print();

  Method entry() { return _entry; }
  char** args() { return _args; }

  // Handling of messages and completions.
  bool has_messages();
  Message* peek_message();
  void remove_first_message();
  int message_count();

  // Signals that a message is for this process.
  void send_mail(Message* message);

  uint64_t random();
  void random_seed(const uint8_t* buffer, size_t size);

  State state() { return _state; }
  void set_state(State state) { _state = state; }

  void add_resource_group(ResourceGroup* r);
  void remove_resource_group(ResourceGroup* r);

  SchedulerThread* scheduler_thread() { return _scheduler_thread; }
  void set_scheduler_thread(SchedulerThread* scheduler_thread) {
    _scheduler_thread = scheduler_thread;
  }

  void signal(Signal signal);
  void clear_signal(Signal signal);
  uint32_t signals() { return _signals; }

  int current_directory() { return _current_directory; }
  void set_current_directory(int fd) { _current_directory = fd; }
  int gc_count() { return _object_heap.gc_count(); }

  // Special allocation of byte arrays and strings due to multiple reasons for failure.
  // The error string is only set if null is returned.
  String* allocate_string(const char* content, Error** error);
  String* allocate_string(int length, Error** error);
  String* allocate_string(const char* content, int length, Error** error);
  Object* allocate_string_or_error(const char* content);
  Object* allocate_string_or_error(const char* content, int length);
  ByteArray* allocate_byte_array(int length, Error** error, bool force_external=false);

  word number_of_blocks() {
    return _object_heap.number_of_blocks();
  }

  void set_max_heap_size(word bytes) {
    _object_heap.set_max_heap_size(bytes);
  }

  bool should_allow_external_allocation(word size) {
    bool result = _object_heap.should_allow_external_allocation(size);
    _object_heap.set_last_allocation_result(result ? Heap::ALLOCATION_SUCCESS : Heap::ALLOCATION_HIT_LIMIT);
    return result;
  }

  bool system_refused_memory() const {
    return _object_heap.system_refused_memory();
  }

  void register_external_allocation(word size) {
    _object_heap.register_external_allocation(size);
  }

  void unregister_external_allocation(word size) {
    _object_heap.unregister_external_allocation(size);
  }

  int64 bytes_allocated_delta() {
    int64 current = object_heap()->total_bytes_allocated();
    int64 result = current - _last_bytes_allocated;
    _last_bytes_allocated = current;
    return result;
  }

  #ifdef PROFILER
   int install_profiler(int task_id) {
     ASSERT(profiler() == null);
     _profiler = _new Profiler(task_id);
     if (_profiler == null) return -1;
     return profiler()->allocated_bytes();
   }
   Profiler* profiler() { return _profiler; }
   void uninstall_profiler() {
     Profiler* p = profiler();
     _profiler = null;
     delete p;
   }
  #endif

  void set_last_run(int64 us) {
    _last_run_us = us;
  }

  void increment_unyielded_for(int64 us) {
    _unyielded_for_us += us;
  }

  void clear_unyielded_for() {
    _unyielded_for_us = 0;
  }

  int64 current_run_duration(int64 now) {
    return _unyielded_for_us + (now - _last_run_us);
  }

 private:
  Process(Program* program, ProcessGroup* group, Block* initial_block);
  void _append_message(Message* message);
  void _ensure_random_seeded();

  int const _id;
  int _next_task_id;

  Program* _program;
  ProcessGroup* _group;

  Method _entry;
  char** _args;
  ObjectHeap _object_heap;
  Usage _memory_usage;
  int64 _last_bytes_allocated;

  MessageFIFO _messages;

  bool _random_seeded;
  uint64_t _random_state0;
  uint64_t _random_state1;

  int _current_directory;

  uint32_t _signals;
  State _state;
  SchedulerThread* _scheduler_thread;

  bool _construction_failed = false;
  bool _idle_since_scavenge = false;

  int64 _last_run_us = 0;
  int64 _unyielded_for_us = 0;

#ifdef PROFILER
  Profiler* _profiler = null;
#endif

  ResourceGroupListFromProcess _resource_groups;
  friend class Scheduler;
};

// A class to manage an allocation and its accounting in the external memory of
// the process.  When the object goes out of scope due to an error condition
// (early return) the allocation is freed and the accounting is updated to
// reflect that. When all conditions are checked and there will be no early
// return, call keep_result() on this object to disable its destructor.
class AllocationManager {
 public:
  explicit AllocationManager(Process* process)
    : _ptr(null)
    , _size(0)
    , _process(process)
    , _hit_limit(false) {}

  AllocationManager(Process* process, void* ptr, word size)
    : _ptr(ptr)
    , _size(size)
    , _process(process)
    , _hit_limit(false) {
    process->register_external_allocation(size);
  }

  uint8_t* alloc(word length) {
    ASSERT(_ptr == null);
    if (!_process->should_allow_external_allocation(length)) {
      _hit_limit = true;
      return null;
    }
    _ptr = malloc(length);
    if (_ptr == null) {
      _process->object_heap()->set_last_allocation_result(Heap::ALLOCATION_OUT_OF_MEMORY);
    } else {
      _process->register_external_allocation(length);
      _size = length;
    }

    return unvoid_cast<uint8_t*>(_ptr);
  }

  uint8_t* calloc(word length, word size) {
    uint8_t* allocation = alloc(length * size);
    if (allocation != null) {
      ASSERT(_size == length * size);
      memset(allocation, 0, _size);
    }
    return allocation;
  }

  ~AllocationManager() {
    if (_ptr != null) {
      free(_ptr);
      _process->unregister_external_allocation(_size);
    }
  }

  uint8_t* keep_result() {
    void* result = _ptr;
    _ptr = null;
    return unvoid_cast<uint8_t*>(result);
  }

 private:
  void* _ptr;
  word _size;
  Process* _process;
  bool _hit_limit;
};

class SystemMessage : public Message {
 public:
  // Some system messages that are created from within the VM.
  enum {
    TERMINATED = 0,
    LOG = 1,
    STACK_TRACE = 2,
  };

  SystemMessage(int type, int gid, int pid, uint8_t* data, int length) : _type(type), _gid(gid), _pid(pid), _data(data), _length(length) { }
  SystemMessage(int type, int gid, int pid) : _type(type), _gid(gid), _pid(pid), _data(null), _length(0) { }
  ~SystemMessage() {
    free(_data);
  }

  MessageType message_type() const { return MESSAGE_SYSTEM; }

  int gid() const { return _gid; }
  int pid() const { return _pid; }
  int type() const { return _type; }

  uint8_t* data() const { return _data; }
  int length() const { return _length; }

  void set_pid(int pid) { _pid = pid; }

  void clear_data() {
    _data = null;
    _length = 0;
  }

 private:
  const int _type;
  const int _gid;  // The process group ID this message comes from.
  int _pid;  // The process ID this message comes from.

  uint8_t* _data;
  int _length;
};

class ObjectNotifyMessage : public Message {
 public:
  explicit ObjectNotifyMessage(ObjectNotifier* notifier)
      : _notifier(notifier)
      , _queued(false) {}
  ~ObjectNotifyMessage() {}

  bool is_queued() { return _queued; }
  void mark_queued() { _queued = true; }
  bool mark_dequeued() {
    _queued = false;
    return _notifier == null;
  }

  MessageType message_type() const { return MESSAGE_OBJECT_NOTIFY; }

  ObjectNotifier* object_notifier() {
    return _notifier;
  }

  bool clear_object_notifier() {
    _notifier = null;
    return !is_queued();
  }

 private:
  ObjectNotifier* _notifier;
  bool _queued;
};

} // namespace toit

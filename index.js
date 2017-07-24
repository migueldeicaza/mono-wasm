// JavaScript WASM support for libc+mono. Inspired from WebAssembly/musl's
// wasm.js file.

Error.stackTraceLimit = Infinity; // print the entire callstack on errors

var debug_logs = true;
var functions = { env: {} };
var module;
var instance;
var heap;
var heap_size;

function heap_get_int(ptr) {
  var d = 0;
  d += (heap[ptr + 0] << 0);
  d += (heap[ptr + 1] << 8);
  d += (heap[ptr + 2] << 16);
  d += (heap[ptr + 3] << 32);
  return d;
}

function heap_set_int(ptr, d) {
  heap[ptr + 0] = ((d & 0x000000ff) >> 0);
  heap[ptr + 1] = ((d & 0x0000ff00) >> 8);
  heap[ptr + 2] = ((d & 0x00ff0000) >> 16);
  heap[ptr + 3] = ((d & 0xff000000) >> 32);
  return d;
}

function heap_get_string(ptr, len=-1) {
  var str = '';
  var i = 0;
  while (true) {
    var c = heap[ptr + i];
    if (c == 0) {
      break;
    }
    if (i == len) {
      break;
    }
    str += String.fromCharCode(c);
    i++;
  }
  return str;
}

function debug(str) {
  if (debug_logs) {
    print(">> " + str);
  }
}

function error(str) {
  print("!! " + str + ": " + new Error().stack);
}

function TerminateWasmException(value) {
  this.message = 'Terminating WebAssembly';
  this.value = value;
  this.toString = function() { return this.message + ': ' + this.value; };
}

function NotYetImplementedException(what) {
  this.message = 'Not yet implemented';
  this.what = what;
  this.toString = function() { return this.message + ': ' + this.what; };
}

load('missing.js')

for (var i in missing_functions) {
  f = missing_functions[i];
  functions['env'][f] = (function(f) { 
    return function() {
      error("Not Yet Implemented: " + f)
      throw new NotYetImplementedException(f);
    }
  })(f);
}

var do_nothing_functions = ['pthread_mutexattr_init', 'pthread_mutexattr_settype', 'pthread_mutex_init', 'pthread_mutexattr_destroy', 'pthread_mutex_lock', 'pthread_mutex_unlock', 'pthread_key_create', 'pthread_condattr_init', 'pthread_condattr_setclock', 'pthread_cond_init', 'pthread_condattr_destroy']

for (var i in do_nothing_functions) {
  f = do_nothing_functions[i];
  functions['env'][f] = function() { }
}

var missing_globals = ['__c_dot_utf8_locale', '__c_locale', '__stderrp',
  'errno', 'mach_task_self_']

for (var i in missing_globals) {
  g = missing_globals[i];
  functions['env'][g] = 0;
}

var syscalls = {}
var syscalls_names = {}

syscalls_names[20] = 'getpid';
syscalls[20] = function() {
  return 42
}

var brk_current = 0
syscalls_names[45] = 'brk';
syscalls[45] = function(inc) {
  if (inc == 0) {
    brk_current = heap_size;
    return brk_current;
  }
  if (brk_current + inc > heap_size) {
    var delta = inc - (heap_size - brk_current)
    brk_current += inc
    var new_pages_needed = Math.ceil(delta / 65536.0)
    var memory = instance.exports.memory
    var n = memory.grow(new_pages_needed);
    debug("grow heap +" + new_pages_needed + " pages from " + n
            + " pages, heap " + heap_size + " -> " + memory.buffer.byteLength)
    heap = new Uint8Array(memory.buffer)
    heap_size = memory.buffer.byteLength
  }
  return inc
}

syscalls_names[54] = 'ioctl';
syscalls[54] = function(fd, req, arg) {
  // TODO
  return 0
}

syscalls_names[76] = 'getrlimit'
syscalls[76] = function(resource, rlim) {
  // TODO
  return 0
}

var out_buffer = '';
syscalls_names[146] = 'write';
syscalls[146] = function(fd, iovs, iov_count) {
  if (fd == 1 || fd == 2) {
    var all_lens = 0
    for (var i = 0; i < iov_count; i++) {
      var base = heap_get_int(iovs + (i * 8))
      var len = heap_get_int(iovs + 4 + (i * 8))
      debug("write fd: " + fd + ", base: " + base + ", len: " + len)
      out_buffer += heap_get_string(base, len)
      all_lens += len
    }
    if (out_buffer.charAt(out_buffer.length - 1) == '\n') {
      print(out_buffer.substr(0, out_buffer.length - 1))
      out_buffer = ''
    }
    return all_lens
  }
  error("can only write on stdout and stderr") 
  return -1
}

var sizeof_k_sigaction = 20
var signals = {} // maps signal numbers to k_sigaction UInt8Array
syscalls_names[174] = 'sigaction'
syscalls[174] = function(sig, act, oact, mask_len) {
  if (mask_len != 8) {
    error('mask_len should be 8 (is ' + mask_len + ')')
    mask_len = 8
  }
  sig_act = (signals[sig] || new Uint8Array(sizeof_k_sigaction))
  if (oact != 0) {
    heap.set(sig_act, oact)    
  }
  if (act != 0) {
    sig_act.set(heap.slice(act, sizeof_k_sigaction)) 
  }
  return 0
}

syscalls_names[175] = 'sigprocmask'
syscalls[175] = function(action, mask, set, sig_n) {
  // TODO
  return 0
}

var process_tid = 42 // Should fix this once we get multithreading
syscalls_names[224] = 'gettid'
syscalls[224] = function() {
  return process_tid
}

syscalls_names[238] = 'tkill'
syscalls[238] = function(tid, signal) {
  if (tid == process_tid) {
    if (signal == 6) {
      // SIGABRT
      error("received SIGABRT")
      throw new TerminateWasmException('SIGABRT');
    }
    error('tkill() with unsupported signal: ' + signal)
  }
  else {
    error('tkill() with wrong tid: ' + tid)
  }
  return -1
}

syscalls_names[252] = 'exit';
syscalls[252] = function(code) {
  debug("exit(" + code + "): " + new Error().stack)
  throw new TerminateWasmException('exit(' + code + ')');
}

syscalls_names[265] = 'clock_gettime';
syscalls[265] = function(clock_id, timespec) {
  // TODO should switch to something else with a higher resolution + support
  // the different CLOCK_ ids.
  if (timespec) {
    var ms = new Date().getTime()
    var sec = Math.floor(ms / 1000)
    var nsec = (ms % 1000) * 1000000
    debug("clock_gettime: msec: " + ms + " -> sec: " + sec + ", nsec: "
            + nsec)
    heap_set_int(timespec, sec)        // tv_sec
    heap_set_int(timespec + 4, nsec)   // tv_nsec
  }
  return 0;
}

syscalls_names[266] = 'clock_getres';
syscalls[266] = function(clock_id, timespec) {
  if (timespec) {
    // Our gettime JS implementation has a 1ms resolution.
    heap_set_int(timespec, 0)           // tv_sec
    heap_set_int(timespec + 4, 1000000) // tv_nsec
  }
  return 0
}

syscalls_names[340] = 'prlimit64'
syscalls[340] = function(pid, resource, new_rlim, old_rlim) {
  // TODO
  return 0
}

function route_syscall() {
  n = arguments[0]
  name = syscalls_names[n]
  if (name) {
    name = "SYS_" + name
  }
  else {
    name = n
  }
  argv = [].slice.call(arguments, 1)
  debug('syscall(' + name + (argv.length > 0 ? ', ' + argv.join(', ') : '')
              + ')')
  f = syscalls[n]
  if (!f) {
    debug('unimplemented syscall ' + n + ' called: ' + new Error().stack)
    return -1
  }
  return f.apply(this, argv)
}

for (var i in [0, 1, 2, 3, 4, 5, 6]) {
  functions['env']['__syscall' + i] = route_syscall
}

module = new WebAssembly.Module(read('index.wasm', 'binary'));
instance = new WebAssembly.Instance(module, functions);

heap = new Uint8Array(instance.exports.memory.buffer);
heap_size = instance.exports.memory.buffer.byteLength;
debug("module heap: " + heap_size)

debug("running main()")
var ret = instance.exports.main();
debug('main() returned: ' + ret);

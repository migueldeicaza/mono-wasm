// JavaScript WASM support for libc+mono. Inspired from WebAssembly/musl's
// wasm.js file.

var debug_logs = true;
var functions = { env: {} };
var module;
var instance;
var heap_size_bytes;
var heap;
var heap_uint8;

function charFromHeap(ptr) { 
  return String.fromCharCode(heap_uint8[ptr]);
}

function stringFromHeap(ptr) {
  var str = '';
  for (var i = ptr; heap_uint8[i] != 0; ++i)
    str += charFromHeap(i);
  return str;
}

function stringFromHeapAndLength(ptr, len) {
  var str = '';
  for (var i = 0; i < len; ++i)
    str += charFromHeap(ptr + i);
  return str;
}

function intFromHeap(ptr) {
  var d = 0;
  d += (heap_uint8[ptr + 0] << 0)
  d += (heap_uint8[ptr + 1] << 8)
  d += (heap_uint8[ptr + 2] << 16)
  d += (heap_uint8[ptr + 3] << 32)
  return d
}

function intToHeap(ptr, d) {
  heap_uint8[ptr + 0] = ((d & 0x000000ff) >> 0)
  heap_uint8[ptr + 1] = ((d & 0x0000ff00) >> 8)
  heap_uint8[ptr + 2] = ((d & 0x00ff0000) >> 16)
  heap_uint8[ptr + 3] = ((d & 0xff000000) >> 32)
  return d
}

function debug(str) {
  if (debug_logs) {
    print("> " + str);
  }
}

function error(str) {
  print("! " + str);
}

function TerminateWasmException(value) {
  this.value = value;
  this.message = 'Terminating WebAssembly';
  this.toString = function() { return this.message + ': ' + this.value; };
}

function NotYetImplementedException(what) {
  this.message = 'Not yet implemented';
  this.what = what;
  this.toString = function() { return this.message + ': ' + this.what; };
}

function NYI(what) {
  return function() { 
    error("Not Yet Implemented: " + new Error().stack);
    throw new NotYetImplementedException(what);
  };
}

// Temporary list of symbols imported by the WASM module that are not
// properly implemented yet.
var missing_functions = ['_Exit', '__addtf3', '__addtf3', '__block_all_sigs',
'__block_app_sigs', '__clone', '__divsc3', '__divtf3', '__divtf3',
'__dl_invalid_handle', '__dl_seterr', '__eqtf2', '__extenddftf2',
'__extendsftf2', '__fdopen', '__fixsfti', '__fixtfdi', '__fixtfdi',
'__fixtfsi', '__fixtfsi', '__fixunstfdi', '__fixunstfsi', '__floatditf',
'__floatscan', '__floatsitf', '__floatsitf', '__floatunditf', '__floatunsitf',
'__getf2', '__gttf2', '__lctrans', '__lctrans_cur', '__libc_sigaction',
'__lock', '__lockfile', '__lttf2', '__lttf2', '__madvise', '__mmap',
'__mremap', '__multf3', '__multf3', '__multi3', '__munmap', '__netf2',
'__netf2', '__nl_langinfo', '__nl_langinfo_l', '__randname',
'__rem_pio2_large', '__restore_sigs', '__set_thread_area', '__stdio_write',
'__stdout_write', '__subtf3', '__synccall', '__syscall_cp', '__trunctfdf2',
'__trunctfsf2', '__unlock', '__unlockfile', '__unordtf2', '__wait',
'_pthread_cleanup_pop', '_pthread_cleanup_push', 'abort', 'backtrace',
'backtrace_symbols', 'btowc', 'cabs', 'clock_get_time', 'clock_sleep',
'closelog', 'execv', 'execve', 'execvp', 'fcntl', 'fdopen', 'feclearexcept',
'fegetround', 'feraiseexcept', 'fesetround', 'fetestexcept', 'fork', 'fprintf',
'freeaddrinfo', 'getaddrinfo', 'getgrgid_r', 'getgrnam_r', 'getloadavg',
'getnameinfo', 'getpriority', 'getprotobyname', 'getpwnam_r', 'getpwuid_r',
'getrusage', 'gettimeofday', 'host_get_clock_service', 'host_page_size',
'host_statistics', 'if_nametoindex', 'ioctl', 'iswctype', 'iswspace', 'kevent',
'localtime_r', 'longjmp', 'mach_absolute_time', 'mach_host_self',
'mach_port_deallocate', 'mach_timebase_info', 'mbrtowc', 'mbsinit',
'mbsnrtowcs', 'mbstowcs', 'mbtowc', 'mincore', 'mini_emit_memcpy',
'mini_gc_set_slot_type_from_cfa', 'mini_gc_set_slot_type_from_fp', 'mkdir',
'mkstemp', 'mmap', 'mono_alloc_freg', 'mono_alloc_ireg',
'mono_allocate_stack_slots', 'mono_arch_instrument_epilog',
'mono_bblock_insert_before_ins', 'mono_call_inst_add_outarg_reg',
'mono_cfg_set_exception_invalid_program', 'mono_compile_create_var',
'mono_decompose_op_imm', 'mono_emit_unwind_op', 'mono_file_map',
'mono_file_unmap', 'mono_file_unmap_fileio', 'mono_get_got_var',
'mono_mark_vreg_as_mp', 'mono_mark_vreg_as_ref', 'mono_peephole_ins',
'mono_print_ins', 'mono_realloc_native_code', 'mono_varlist_sort', 'msync',
'munmap', 'nanosleep', 'nl_langinfo', 'open', 'openlog', 'posix_spawn',
'posix_spawn_file_actions_adddup2', 'posix_spawn_file_actions_destroy',
'posix_spawn_file_actions_init', 'proc_pidpath', 'pthread_attr_init',
'pthread_attr_setdetachstate', 'pthread_barrier_init', 'pthread_barrier_wait',
'pthread_cond_timedwait_relative_np', 'pthread_create', 'pthread_equal',
'pthread_get_stackaddr_np', 'pthread_get_stacksize_np', 'pthread_getname_np',
'pthread_getschedparam', 'pthread_getspecific', 'pthread_key_create',
'pthread_key_delete', 'pthread_main_np', 'pthread_mutex_init',
'pthread_mutex_lock', 'pthread_mutex_unlock', 'pthread_once', 'pthread_self',
'pthread_setcancelstate', 'pthread_setschedparam', 'pthread_setspecific',
'raise', 'sched_get_priority_max', 'sched_get_priority_min',
'semaphore_create', 'semaphore_destroy', 'semaphore_signal',
'semaphore_timedwait', 'semaphore_wait', 'setitimer', 'setjmp', 'setlocale',
'setpriority', 'sigaction', 'signal', 'sigprocmask', 'snprintf', 'socket',
'sprintf', 'stat', 'statfs', 'statvfs', 'strdup', 'strftime', 'sysconf',
'sysctl', 'syslog', 'task_for_pid', 'task_info', 'task_threads', 'tcflush',
'tcgetattr', 'tcsetattr', 'thread_get_state', 'thread_info',
'thread_set_state', 'time', 'towlower', 'towupper', 'uname', 'utime',
'utimensat', 'vfprintf', 'vfscanf', 'vm_deallocate', 'vsnprintf', 'waitpid',
'wcsrtombs', 'wctomb', 'wctype']

for (var i in missing_functions) {
  f = missing_functions[i]
  functions['env'][f] = NYI(f);
}

var missing_globals = ['__c_dot_utf8_locale', '__c_locale', '__stderrp',
  'errno', 'mach_task_self_']

for (var i in missing_globals) {
  g = missing_globals[i]
  functions['env'][g] = 0;
}

var syscalls = {}
var syscalls_names = {}

syscalls_names[45] = 'brk';
syscalls[45] = function(inc) {
  if (inc == 0) {
    return heap_size_bytes;
  }
  if (inc > heap_size_bytes) {
    var delta = inc - heap_size_bytes
    var new_pages_needed = Math.ceil(delta / 65536.0)
    var memory = instance.exports.memory
    var n = memory.grow(new_pages_needed);
    debug("grow heap +" + new_pages_needed + " pages from " + n
            + " pages, new heap " + memory.buffer.byteLength)
    heap_uint8 = new Uint8Array(memory.buffer)
    heap_size_bytes = memory.buffer.byteLength
  }
  return inc
}

syscalls_names[54] = 'ioctl';
syscalls[54] = function(fd, req, arg) {
  // TODO
  return 0
}

var out_buffer = '';
syscalls_names[146] = 'write';
syscalls[146] = function(fd, iovs, iov_count) {
  if (fd == 1 || fd == 2) {
    var all_lens = 0
    for (var i = 0; i < iov_count; i++) {
      var base = intFromHeap(iovs + (i * 8))
      var len = intFromHeap(iovs + 4 + (i * 8))
      debug("write fd: " + fd + ", base: " + base + ", len: " + len)
      out_buffer += stringFromHeapAndLength(base, len)
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

syscalls_names[252] = 'exit';
syscalls[252] = function(code) {
  debug("exit(" + code + "): " + new Error().stack)
  throw new TerminateWasmException('SYS_exit_group(' + code + ')');
}

syscalls_names[265] = 'clock_gettime';
syscalls[265] = function(clock_id, timespec) {
  if (clock_id == 0) {
    var ms = new Date().getTime()
    var sec = Math.floor(ms / 1000)
    var usec = (ms % 1000) * 1000
    debug("clock_gettime: msec: " + ms + " -> sec: " + sec + ", usec: " + usec)
    intToHeap(timespec, sec)
    intToHeap(timespec + 4, usec)
    return 0;
  }
  debug("invalid clock_id " + clock_id)
  return -1
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
  debug('syscall(' + name + ', ' + argv + ')')
  f = syscalls[n]
  return f ? f.apply(this, argv) : -1
}

for (var i in [0, 1, 2, 3, 4, 5, 6]) {
  functions['env']['__syscall' + i] = route_syscall
}

module = new WebAssembly.Module(read('index.wasm', 'binary'));
instance = new WebAssembly.Instance(module, functions);

heap = instance.exports.memory.buffer;
heap_uint8 = new Uint8Array(heap);
heap_size_bytes = heap.byteLength;
debug("module heap: " + heap_size_bytes)

debug("running main()")
var ret = instance.exports.main();
debug('main() returned: ' + ret);

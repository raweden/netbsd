// lwp.js 
// sys/arch/wasm/bindings/lwp.js

// startup subrutine for a userland process/thread 
// The core concept is to memic a real kernel as close as possible, before process is exec the target *.wasm
// the kernel clones itself into the worker, in theory this should not create that much of a overhead when
// using shared memory and sharing the module (which according to spec is not cloned but shared) so it should
// basically be state data and stack data which are the overhead.
// 
// For emscripten a simple wrapper/adapter could be implemented which maps __syscall_* to central syscall dispatch.
// 
// Where does syscall enter in freebsd, what path does it take?
// - its seams to be implemented in syscallenter() which takes the chosen syscall handler via sysent->sy_call (from struct syscall_args)
// 
// Per Instance:
// - stack memory (64kib ?) td_kstack_pages

const _kexp = {};

(function() {

	let __curlwp;
	let __stack_pointer;
	let kmemory;	// WebAssembly.Memory
	let kmembuf;	// SharedArrayBuffer
	let kheapu8;	// Uint8Array
	let kheap32;	// Int32Array
	let kmem;		// DataView

	let _uvm_spacep;
	let umemory;
	let uheapu8;	// Uint8Array
	let umem;		// DataView
	let utmp;

	let _exports;
	let _uinstance;
	const unwind4exec = {msg: "stack-unwinder for exec syscall"};

	// checks so that we are not holding onto a old ref to kernel memory,
	// might happen if another thread calls memory.grow
	function __check_kmem() {
		if (kmemory.buffer.byteLength === kmembuf.byteLength)
			return;

		kmembuf = kmemory.buffer;
		kheap32 = new Int32Array(kmembuf);
		kheapu8 = new Uint8Array(kmembuf);
		kmem = new DataView(kmembuf);
	}

	// detect and update after memory.grow for umemory
	function __check_umem() {
		if (umemory.buffer.byteLength === uheapu8.buffer.byteLength || uheapu8 === utmp)
			return;

		uheapu8 = new Uint8Array(umemory.buffer);
		umem = new DataView(uheapu8.buffer);
	}

	function __check_mem() {
		__check_kmem();
		__check_umem();
	}

	function alignUp(x, multiple) {
	  	if (x % multiple > 0) {
	    	x += multiple - (x % multiple);
	  	}
	  	return x;
	}

	function u8_memcpy(src, sidx, slen, dst, didx) {
	    // TODO: remove this assert at later time. (should be a debug)
	    if (!(src instanceof Uint8Array) && (dst instanceof Uint8Array)) {
	        throw TypeError("src and dst Must be Uint8Array");
	    }
	    //console.log(src, dst);
	    let idx = sidx;
	    let end = idx + slen;
	    /*if (slen > 512) {
	        let subarr = src.subarray(idx, end);
	        dst.set(subarr, didx);
	        return;
	    }*/

	    while(idx < end) {
	        dst[didx++] = src[idx++];
	    }
	}

	// from emscripten.
	let UTF8Decoder = typeof TextDecoder !== 'undefined' ? new TextDecoder('utf8') : undefined;

	function UTF8ArrayToString(heap, idx, maxBytesToRead) {
	    var endIdx = idx + maxBytesToRead;
	    var endPtr = idx;
	    // TextDecoder needs to know the byte length in advance, it doesn't stop on null terminator by itself.
	    // Also, use the length info to avoid running tiny strings through TextDecoder, since .subarray() allocates garbage.
	    // (As a tiny code save trick, compare endPtr against endIdx using a negation, so that undefined means Infinity)
	    while (heap[endPtr] && !(endPtr >= endIdx)) ++endPtr;

	    var str = '';
	    // If building with TextDecoder, we have already computed the string length above, so test loop end condition against that
	    while (idx < endPtr) {
	        // For UTF8 byte structure, see:
	        // http://en.wikipedia.org/wiki/UTF-8#Description
	        // https://www.ietf.org/rfc/rfc2279.txt
	        // https://tools.ietf.org/html/rfc3629
	        var u0 = heap[idx++];
	        if (!(u0 & 0x80)) { str += String.fromCharCode(u0); continue; }
	        var u1 = heap[idx++] & 63;
	        if ((u0 & 0xE0) == 0xC0) { str += String.fromCharCode(((u0 & 31) << 6) | u1); continue; }
	        var u2 = heap[idx++] & 63;
	        if ((u0 & 0xF0) == 0xE0) {
	            u0 = ((u0 & 15) << 12) | (u1 << 6) | u2;
	        } else {
	            if ((u0 & 0xF8) != 0xF0) warnOnce('Invalid UTF-8 leading byte 0x' + u0.toString(16) + ' encountered when deserializing a UTF-8 string in wasm memory to a JS string!');
	            u0 = ((u0 & 7) << 18) | (u1 << 12) | (u2 << 6) | (heap[idx++] & 63);
	        }

	        if (u0 < 0x10000) {
	            str += String.fromCharCode(u0);
	        } else {
	            var ch = u0 - 0x10000;
	            str += String.fromCharCode(0xD800 | (ch >> 10), 0xDC00 | (ch & 0x3FF));
	        }
	    }
	    return str;
	}

	function stringToUTF8Bytes(heap, off, str) {

	    let start = off;
	    let end = heap.byteLength;
	    for (let i = 0; i < str.length; ++i) {
	        // Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code unit, not a Unicode code point of the character! So decode UTF16->UTF32->UTF8.
	        // See http://unicode.org/faq/utf_bom.html#utf16-3
	        // For UTF8 byte structure, see http://en.wikipedia.org/wiki/UTF-8#Description and https://www.ietf.org/rfc/rfc2279.txt and https://tools.ietf.org/html/rfc3629
	        let u = str.charCodeAt(i); // possibly a lead surrogate
	        if (u >= 0xD800 && u <= 0xDFFF) {
	            let u1 = str.charCodeAt(++i);
	            u = 0x10000 + ((u & 0x3FF) << 10) | (u1 & 0x3FF);
	        }
	        if (u <= 0x7F) {
	            if (off >= end)
	                break;
	            heap[off++] = u;
	        } else if (u <= 0x7FF) {
	            if (off + 1 >= end)
	                break;
	            heap[off++] = 0xC0 | (u >> 6);
	            heap[off++] = 0x80 | (u & 63);
	        } else if (u <= 0xFFFF) {
	                
	            if (off + 2 >= end)
	                break;
	            heap[off++] = 0xE0 | (u >> 12);
	            heap[off++] = 0x80 | ((u >> 6) & 63);
	            heap[off++] = 0x80 | (u & 63);
	        } else {
	            if (off + 3 >= end)
	                break;
	            if (u > 0x10FFFF)
	                console.warn('Invalid Unicode code point 0x' + u.toString(16) + ' encountered when serializing a JS string to a UTF-8 string in wasm memory! (Valid unicode code points should be in range 0-0x10FFFF).');
	            heap[off++] = 0xF0 | (u >> 18);
	            heap[off++] = 0x80 | ((u >> 12) & 63);
	            heap[off++] = 0x80 | ((u >> 6) & 63);
	            heap[off++] = 0x80 | (u & 63);
	        }
	    }

	    return off;
	}

	// i32 kern.__copyout(i32, i32, i32)
	function kcopyout(kaddr, uaddr, len) {
		__check_mem();
		u8_memcpy(kheapu8, kaddr, len, uheapu8, uaddr);
		return 0;
	}

	// copies a string from kernel to user
	// i32 kern.__copyoutstr(i32, i32, i32, i32)
	// 
	// copyoutstr(const void *kaddr, void *uaddr, size_t len, size_t *done)
	// @see sys/arch/riscv/riscv/trap.c
	function kcopyoutstr(kaddr, uaddr, len, lenptr) {
		__check_mem();
		let cnt = 0;
		let end = -1;

		if (len !== 0) {
			while (cnt < len) {
				let byte = kheapu8[uaddr + cnt];
				uheapu8[kaddr + cnt] = byte;
				cnt++;
				if (byte == 0) {
					end = cnt;
					break;
				}
			}
		}

		if (lenptr !== 0) {
			kmem.setUint32(lenptr, cnt, true);
		}

		// returns ENAMETOOLONG when null char is not found
		return end !== -1 ? 0 : 37; // ENAMETOOLONG
	}


	// i32 kern.__copyin(i32, i32, i32)
	function kcopyin(uaddr, kaddr, len) {
		__check_mem();
		u8_memcpy(uheapu8, uaddr, len, kheapu8, kaddr);
		return 0;
	}

	// copies a string from user to kernel
	// i32 kern.__copyinstr(i32, i32, i32, i32)
	// 
	// int copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *done)
	// @see sys/riscv/riscv/copyinout.S
	function kcopyinstr(uaddr, kaddr, len, lenptr) {
		__check_mem();
		let cnt = 0;
		let end = -1;

		if (len !== 0) {
			while (cnt < len) {
				let byte = uheapu8[uaddr + cnt];
				kheapu8[kaddr + cnt] = byte;
				cnt++;
				if (byte == 0) {
					end = cnt;
					break;
				}
			}
		}

		if (lenptr !== 0) {
			kmem.setUint32(lenptr, cnt, true);
		}

		// returns ENAMETOOLONG when null char is not found
		return end !== -1 ? 0 : 37; // ENAMETOOLONG
	}

	// determine the length of a userspace string
	// i32 kern.__copyinstr(i32, i32, i32, i32)
	// 
	// int instrlen(const void *udaddr, size_t maxlen, size_t *done)
	// @see sys/riscv/riscv/copyinout.S
	function kern_instrlen(uaddr, maxlen, lenptr) {
		__check_mem();
		let cnt = 0;
		let end = -1;

		if (maxlen !== 0) {
			while (cnt < maxlen) {
				let byte = uheapu8[uaddr + cnt];
				cnt++;
				if (byte == 0) {
					end = cnt;
					break;
				}
			}
		}

		if (lenptr !== 0) {
			kmem.setUint32(lenptr, cnt, true);
		}

		// returns ENAMETOOLONG when null char is not found
		return end !== -1 ? 0 : 37; // ENAMETOOLONG
	}

	// i32 kern.__fetch_user_data(i32, i32, i32)
	function kfetch_user_data(uaddr, kaddr, len) {
		__check_mem();
		u8_memcpy(uheapu8, uaddr, len, kheapu8, kaddr);
		return 0;
	}

	// i32 kern.__store_user_data(i32, i32, i32)
	function kstore_user_data(uaddr, kaddr, len) {
		__check_mem();
		u8_memcpy(kheapu8, kaddr, len, uheapu8, uaddr);
		return 0;
	}

	// i32 kern.ucas32(i32, i32, i32, i32)
	function kern_ucas32(uaddr, oval, nval, ret)
	{
		if (uaddr % 4 !== 0) 
			console.error("unaligned");

		let old = Atomics.compareExchange(HEAP32, uaddr / 4, oval, nval);
		if (ret !== 0) {
			kmemdata.setUint32(ret, old, true);
		}
		
		return 0;
	}

	// [i32] → []
	function kcsr_sstatus_clear(val) {
		// from user space
		return;
	}

	// [] → [i32]
	function kcsr_sstatus_read() {
		// from user space
		return 0;
	}

	// [i32] → []
	function kcsr_sstatus_set(val) {
		// from user space
		return;
	}

	// [i32] → []
	function ksbi_extcall(tf) {
		// from user space
		return;
	}

	// void kern.panic_abort(void)
	function kern_panic() {
		throw new Error("kernel_panic");
	}


	// void kern.random_source(i32, i32)
	function kern_random_source(buf, bufsz) {
		let tmp = new Uint8Array(bufsz);
	    crypto.getRandomValues(tmp);
	    __check_kmem();
	    u8_memcpy(tmp, 0, bufsz, kheap32, buf);
	    return bufsz;
	}

	function wasm_sched_throw(td) {
		console.log("sched_throw td = %d", td);
		self.postMessage({cmd: "wasm_sched_throw"});
		setTimeout(function() {
			self.close();
		}, 0);
		throw new Error("KERN_SCHED_THROW");
	}

	// i32 emscripten.memcpy_big(i32, i32, i32)
	function emscripten_memcpy_big(dst, src, len) {
		__check_kmem();
		kheapu8.copyWithin(dst, src, src + len);
	}

	function kthread_dispatch_sync(fn) {
		self.postMessage();
	}

	function kthread_dispatch_sync_i32(fn) {
		self.postMessage();
	}

	function kthread_dispatch_sync_i64(fn) {
		self.postMessage();
	}

	function kthread_dispatch_sync_void(fn) {
		self.postMessage();
	}

	function kthread_dispatch_nowait(fn) {
		// copies whats in td->td_frame and dispatch call but do not block and allows no return data.
	}

	// void kern.__copyout(i32, i32, i32, i32)
	function kern_console_log(buf, bufsz, flags, level) {
		let str = UTF8ArrayToString(kheapu8, buf, bufsz);
		if (str.startsWith("panic:")) {
			console.error(str);
		} else {
			console.log(str);
		}
	}

	// user imports
	
	function usr_stderr_stdout_write(fd, buf, bufsz) {
		let str = UTF8ArrayToString(uheapu8, buf, bufsz);
		str = str.trim();
		if (fd == 1) {
			console.log("%c%s", "color: #ffc107;font-family:monospace; background-color: #181818; padding: 3px;", str);
		} else if (fd == 2) {
			console.warn("%c%s", "color: #e50404;font-family:monospace; background-color: #181818; padding: 3px;", str);
		}
	}

	// void kern.random_source(i32, i32)
	// only diffrence from the kernel version is the destination memory.
	function usr_random_source(buf, bufsz) {
		let tmp = new Uint8Array(bufsz);
	    crypto.getRandomValues(tmp);
	    __check_umem();
	    u8_memcpy(tmp, 0, bufsz, uheapu8, buf);
	    return bufsz;
	}

	function syscall_trap(user_tf) {
		_exports.syscall_trap_handler(user_tf);
	}

	function usr_emscripten_get_heap_max() {
		return umemory.buffer.maxByteLength;
	}

	function usr_emscripten_get_heap_size() {
		return umemory.buffer.byteLength;
	}

	function usr_emscripten_resize_heap(requestedSize) {
		let oldSize = umemory.buffer.byteLength;
		requestedSize = requestedSize >>> 0;
		if (!(requestedSize > oldSize)) {
			console.error("requestedSize <= oldSize");
			throw new Error("requestedSize <= oldSize");
		}

		let pages = Math.ceil((requestedSize - oldSize) / 65535);
		/*
		let overGrownHeapSize, maxHeapSize = usrmem.buffer.maxByteLength;
		if (requestedSize > maxHeapSize) {
			throw new Error('Cannot enlarge memory, asked to go up to ' + requestedSize + ' bytes, but the limit is ' + maxHeapSize + ' bytes!');
			return false;
		}

		// Loop through potential heap size increases. If we attempt a too eager reservation that fails, cut down on the
		// attempted size and reserve a smaller bump instead. (max 3 times, chosen somewhat arbitrarily)
		for (let cutDown = 1; cutDown <= 4; cutDown *= 2) {
			overGrownHeapSize = oldSize * (1 + 0.2 / cutDown); // ensure geometric growth
		}
		
		// but limit overreserving (default to capping at +96MB overgrowth at most)
		overGrownHeapSize = Math.min(overGrownHeapSize, requestedSize + 100663296 );

		let newSize = Math.min(maxHeapSize, alignUp(Math.max(requestedSize, overGrownHeapSize), 65536));
		*/
		try {
			// round size grow request up to wasm page size (fixed 64KB per spec)
    		umemory.grow(pages);
		} catch (err) {
			console.log(err);
			throw err;
			return false;
		}

		return true;
	}


	// executing from kern_exec()
	let _execbuf;
	let _execmod;
	let _execusr; // user executable instance.
	let _ustkptr;
	let _uheapbase;
	let _forkframe;
	let _meminfo;

	function post_mmblkd_attach(memory, vm_space_ptr) {
		if (vm_space_ptr === 0)
			throw new RangeError("mmblkd_attach is NULL");
		
		self.postMessage({
			cmd: "mmblkd_attach",
			memory: memory, 		// special role = "kmemory" key, normal memory = "memory" key
			meminfo_ptr: vm_space_ptr
		});
	}

	function wasm_thread_alloc(td) {
		/*try {
			throw new Error("FAKE_ERROR");
		} catch (err) {
			console.error(err);
			console.log(err.stack);
		}*/
		let wqidx = Math.min(__stack_pointer.value / 4) - 1;
		Atomics.store(kheap32, wqidx, 0);
		self.postMessage({cmd: "wasm_thread_alloc", args: [td], wqidx: wqidx});
		Atomics.wait(kheap32, wqidx, 0);
	}

	function klwp_spawn(l1, l2, stackptr, stacksz) {
		console.error("klwp_spawn l1 = %d l2 = %d stackptr = %d stacksz = %d", l1, l2, stackptr, stacksz);
	}

	function wasm_sched_add(td, flags) {
		let in_fork = _forkframe.fork_from;
		let fork_args = _forkframe.fork_args;
		let wqidx = Math.min(__stack_pointer.value / 4) - 1;
		Atomics.store(kheap32, wqidx, 0);
		self.postMessage({cmd: "wasm_sched_add", args: [td, flags], wqidx: wqidx, user_memory: umemory, _uvm_spacep: _uvm_spacep, user_module: _execmod, in_fork: in_fork, fork_frame: fork_args});
		Atomics.wait(kheap32, wqidx, 0);
	}
	function wasm_sched_fork(td, childtd) {
		_forkframe = {};
		_forkframe.fork_from = _execusr.exports.fork_from.value;
		_forkframe.fork_args = [_execusr.exports.fork_arg.value];
		_execusr.exports.fork_from.value = 0;
		_execusr.exports.fork_arg.value = 0;
		let ustack_sz = _meminfo && Number.isInteger(_meminfo.default_stack_size) ? Math.ceil(_meminfo.default_stack_size / 4096) * 4096 : 65536;
		let ustack_ptr = _execusr.exports.__libc_malloc(ustack_sz); // __libc_free
		if (ustack_ptr == 0) {
			throw new RangeError("ENOCORE");
		}
		let wqidx = Math.min(__stack_pointer.value / 4) - 1;
		Atomics.store(kheap32, wqidx, 0);
		self.postMessage({cmd: "wasm_sched_fork", args: [td, childtd], ustack: ustack_ptr, wqidx: wqidx});
		Atomics.wait(kheap32, wqidx, 0);
	}

	function kexecbuf_alloc(size) {
		if (_execbuf != undefined) {
			console.error("Invalid state; execbuf already exists");
			return;
		}
		_execbuf = new Uint8Array(size);
		return 0;
	}

	function kexecbuf_copy(kaddr, uaddr, size) {
		if (_execbuf == undefined) {
			console.error("Invalid state; no execbuf");
			return;
		}
		u8_memcpy(kheapu8, kaddr, size, _execbuf, uaddr);
		return 0;
	}

	function kexec_finish() {
		libexec_prepare_wasm();
		// throw error to undwind call stack
		throw unwind4exec;
	}

	function libexec_prepare_wasm() {
		
		if (_execbuf == undefined) {
			console.error("Invalid state; no execbuf");
			return;
		}

		if (_uvm_spacep) {
			_exports.deref_uvmspace(_uvm_spacep);
			_uvm_spacep = undefined;
		}

		_execmod = new WebAssembly.Module(_execbuf);
		_execbuf = undefined; // lose the buffer
		console.log(_execmod);
		console.log(WebAssembly.Module.exports(_execmod));
		console.log(WebAssembly.Module.imports(_execmod));

		// getting information on either imported or exported memory 
		let imports = WebAssembly.Module.imports(_execmod);
		let memoryImport, memoryExport;
		let len = imports.length;
		for (let i = 0; i < len; i++) {
			let imp = imports[i];
			if (imp.kind != "memory")
				continue;
			memoryImport = imp;
		}
		// TODO: also check for __stack_pointer in imports,

		if (!memoryImport) {
			let exported = WebAssembly.Module.exports(_execmod);
			let len = exported.length;
			for (let i = 0; i < len; i++) {
				let exp = exported[i];
				if (exp.kind != "memory")
					continue;
				memoryExport = exp;
			}
		}

		const imgactSections = WebAssembly.Module.customSections(_execmod, "tinybsd.wasm_imgact");
		if (imgactSections.length !== 0) {
			console.log("Module contains a tinybsd.wasm_imgact section");
			let buf = new Uint8Array(imgactSections[0]);
			let str = UTF8ArrayToString(buf, 0, buf.byteLength);
			_meminfo = JSON.parse(str);
			console.log(_meminfo);
		}

		_uheapbase = _meminfo ? _meminfo.default_stack_pointer : 102496; 				// TODO: use __stackpointer + [argv + envv] == __heapbase as the stack size might not be enough otherwise.
		_ustkptr = new WebAssembly.Global({value: 'i32', mutable: true}, _uheapbase);
		//let exp = _kexp.initialize();
		let importObject = {
			kern: {
				random_source: usr_random_source,
				panic_abort: kern_panic,
				__stack_pointer: _ustkptr,
				usr_stderr_stdout_write: usr_stderr_stdout_write
			},
			sys: {
				syscall_trap: syscall_trap,
				sbrk: usbrk,
			},
			emscripten: {
				get_heap_size: usr_emscripten_get_heap_size,
				get_heap_max: usr_emscripten_get_heap_max,
				resize_heap: usr_emscripten_resize_heap,
				get_heap_init: __wasm_imgact_get_heap_base,
			}
		};

		// if imported memory we need to create it here.
		if (memoryImport) {
			let m = memoryImport.module;
			let n = memoryImport.name;

			if (!importObject.hasOwnProperty(m)) {
				importObject[m] = {};
			}

			let initial = memoryImport.type.minimum;
			let maximum = memoryImport.type.maximum;
			let shared = memoryImport.type.shared

			umemory = new WebAssembly.Memory({initial: initial, maximum: maximum, shared: shared});
	    	umem = new DataView(umemory.buffer);
	    	uheapu8 = new Uint8Array(umemory.buffer);

			if (shared) {
				_uvm_spacep = _exports.new_uvmspace();
				post_mmblkd_attach(umemory, vm_space_ptr);
			}

			importObject[m][n] = umemory;
		}

		let instance = new WebAssembly.Instance(_execmod, importObject);
		let _exports = instance.exports;
		//exp.instance = instance;
		console.log(instance);
		console.log(_exports);
		_execusr = instance;

		if (memoryExport) {
			umemory = _exports[memoryExport.name];
	    	umem = new DataView(umemory.buffer);
	    	uheapu8 = new Uint8Array(umemory.buffer);

			if (umemory.buffer instanceof SharedArrayBuffer) {
				_uvm_spacep = _exports.new_uvmspace();
				post_mmblkd_attach(umemory, _uvm_spacep);
			}
		}
	}

	function __wasm_imgact_run(argc, argv) {

		let ret;
		try {
			ret = _execusr.exports.main(argc, argv);
		} catch (err) {
			console.error(err);
			throw err;
		}
		console.log("main ret = %d", ret);
	}

	function __wasm_imgact_getstackptr() {
		if (_ustkptr == undefined) {
			console.error("Invalid state; no execusr");
			return 0;
		}

		return _ustkptr.value;
	}

	function __wasm_imgact_setstackptr(value) {
		if (_ustkptr == undefined) {
			console.error("Invalid state; no execusr");
			return;
		}

		_ustkptr.value = value;
		return;
	}

	function __wasm_imgact_set_heap_base(value) {
		_uheapbase = value;
	}

	function __wasm_imgact_get_heap_base() {
		return _uheapbase;
	}

	function usbrk(arg) {
		console.warn("where is this called!");
		return 1024;
	}

	function wasm_clock_gettime(ts) {

		let now = performance.timeOrigin + performance.now();

		kmem.setInt32(ts, (now / 1000) | 0, true);					// seconds
		kmem.setInt32(ts + 4, ((now % 1000)*1000*1000) | 0, true);	// nanoseconds

		return 0;
	}

	// netbsd specific
	
	function exec_entrypoint(argc, argv) {

		let ret;

		// __wasm_call_ctors must be called before any entry into binary.
		if (typeof _execusr.exports.__wasm_call_ctors == "function") {
			_execusr.exports.__wasm_call_ctors();
		}

		try {
			ret = _execusr.exports.main(argc, argv);
		} catch (err) {
			console.error(err);
			throw err;
		}
		console.log("main ret = %d", ret);
	}

	function kspawn_blkdev_worker(rblkdev, init_cmd) {
		throw new Error("kspawn_blkdev_worker must be called on lwp0");
	}
	
	// void kern.cons_write(i32, i32, i32, i32) 	[NOT USED!]
	function kcons_write(buf, bufsz, flags, level) {
		let str = UTF8ArrayToString(kheapu8, buf, bufsz);
		if (str.startsWith("panic:")) {
			console.error(str);
		} else {
			str.trimEnd();
			console.log("%c%s", "font-family: monospace;", str);
		}
		
	}

	function __kexec_ualloca(size) {
		if (uheapu8 != undefined)
			throw TypeError("uheapu8 already allocated");
		uheapu8 = new Uint8Array(size);
		utmp = uheapu8;
		console.log("__kexec_ualloca called");
		return size;
	}

	function __lock_debug(lockp, action) {
		//let action_name = ["INIT", "LOCK", "UNLOCK", "FAILED"];
		//console.log("cpulock = %d action = %s lwp = %d", lockp, action_name[action], __curlwp);
	}

	function kexec_ioctl(cmd, argp) {
		switch (cmd) {

			case 512: {	// get user stack-pointer
				if (argp !== 0) {
					kmem.setUint32(argp, _ustkptr.value, true);
				}
				break;
			}

			case 513: { // set user stack-pointer
				_ustkptr.value = argp;
				break;
			}
		}
	}

	/*function syscall_trap() {
		//let stackptr = _uinstance.
		console.log(_uinstance);
		// copy in; copy value of userland td_sa (110624) to kernel-land td->td_sa
		u8_memcpy(uheapu8, 110624, 80, kheapu8, __curlwp + 672);
		_exports.do_syscall_handler();
		// copy out; copy value of kernel-land &td->td_frame to userland td_frame
		let ptr = kmem.getUint32(__curlwp + 840);
		// offset of td->td_sa 672
		// offset of td->td_frame 800
	}*/

	_kexp.syscall_trap = syscall_trap;
	_kexp.random_source = kern_random_source;

	function lwp_trampoline() {

	}

	function spawn_thread(opts) {
		kmemory = opts.kernel_memory;
		kmembuf = kmemory.buffer;
		kheap32 = new Int32Array(kmembuf);
		kheapu8 = new Uint8Array(kmembuf);
		kmem = new DataView(kmembuf);
		__curlwp = opts.__curlwp;

		let userfork = false;
		
		// kernel module (executable instance)
		let importObject = {
			env: {
				memory: kmemory,
			},
			kern: {
				__curlwp: new WebAssembly.Global({value: 'i32', mutable: false}, opts.__curlwp),
				__stack_pointer: new WebAssembly.Global({value: 'i32', mutable: true}, opts.__stack_pointer),
				__copyout: kcopyout,
				__copyoutstr: kcopyoutstr,
				__copyin: kcopyin,
				__copyinstr: kcopyinstr,
				__fetch_user_data: kfetch_user_data,
				__store_user_data: kstore_user_data,
				__lwp_spawn: klwp_spawn,
				panic_abort: kern_panic,
				// not used yet..
				random_source: kern_random_source,
				console_log: kern_console_log,
				cons_write: kcons_write,
				kthread_dispatch_sync: kthread_dispatch_sync,
				wasm_thread_alloc: wasm_thread_alloc,
				wasm_sched_add: wasm_sched_add,
				sched_fork: wasm_sched_fork,
				wasm_sched_throw, wasm_sched_throw,
				wasm_clock_gettime: wasm_clock_gettime,
				// netbsd spec
				csr_sstatus_clear: kcsr_sstatus_clear,
				csr_sstatus_read: kcsr_sstatus_read,
				csr_sstatus_set: kcsr_sstatus_set,
				sbi_extcall: ksbi_extcall,
				ucas32: kern_ucas32,
				spawn_blkdev_worker: kspawn_blkdev_worker,
				kexec_ualloca: __kexec_ualloca,
				__lock_debug: __lock_debug,
				// kern_exec
				execbuf_alloc: kexecbuf_alloc,
				execbuf_copy: kexecbuf_copy,
				exec_finish: kexec_finish,			// called from wasm to replace the current user execuble with the one loaded into _execbuf
				exec_entrypoint: exec_entrypoint,
				exec_ioctl: kexec_ioctl,
			},
			emscripten: {
				memcpy_big: emscripten_memcpy_big,
			}
		};

		__curlwp = opts.__curlwp;
		__stack_pointer = importObject.kern.__stack_pointer;
		let instance = new WebAssembly.Instance(opts.kernel_module, importObject);
		_exports = instance.exports;

		// user module (executable instance)
		console.log("spawned %s thread", globalThis.name);

		if (opts.user_module) {
			_execmod = opts.user_module;
			let user_memory = opts.user_memory;
			if (opts._uvm_spacep) {
				_uvm_spacep = opts._uvm_spacep;
			}

			// getting information on either imported or exported memory 
			let imports = WebAssembly.Module.imports(_execmod);
			let memoryImport, memoryExport;
			let len = imports.length;
			for (let i = 0; i < len; i++) {
				let imp = imports[i];
				if (imp.kind != "memory")
					continue;
				memoryImport = imp;
			}
			// TODO: also check for __stack_pointer in imports,

			if (!memoryImport) {
				let exported = WebAssembly.Module.exports(_execmod);
				memoryExport
				let len = exported.length;
				for (let i = 0; i < len; i++) {
					let exp = exported[i];
					if (exp.kind != "memory")
						continue;
					memoryExport = exp;
				}
			}

			_uheapbase = 102496; // TODO: use __stackpointer + [argv + envv] == __heapbase as the stack size might not be enough otherwise.
			let _ustack_ptr = _uheapbase;
			if (Number.isInteger(opts.ustack)) {
				_ustack_ptr = opts.ustack
			}

			_ustkptr = new WebAssembly.Global({value: 'i32', mutable: true}, _ustack_ptr);
			//let exp = _kexp.initialize();
			let importObject = {
				kern: {
					random_source: usr_random_source,
					panic_abort: kern_panic,
					__stack_pointer: _ustkptr,
					usr_stderr_stdout_write: usr_stderr_stdout_write
				},
				sys: {
					syscall_trap: syscall_trap,
					sbrk: usbrk,
				},
				emscripten: {
					get_heap_size: usr_emscripten_get_heap_size,
					get_heap_max: usr_emscripten_get_heap_max,
					resize_heap: usr_emscripten_resize_heap,
					get_heap_init: __wasm_imgact_get_heap_base,
				}
			};

			// if imported memory we need to create it here.
			if (memoryImport) {
				let m = memoryImport.module;
				let n = memoryImport.name;

				if (!importObject.hasOwnProperty(m)) {
					importObject[m] = {};
				}

				if (!user_memory) {
					let initial = memoryImport.type.minimum;
					let maximum = memoryImport.type.maximum;
					let shared = memoryImport.type.shared

					umemory = new WebAssembly.Memory({initial: initial, maximum: maximum, shared: shared});

					if (shared) {
						_uvm_spacep = _exports.new_uvmspace();
						post_mmblkd_attach(umemory, _uvm_spacep);
					}
				} else {
					umemory = user_memory;
				}

		    	umem = new DataView(umemory.buffer);
		    	uheapu8 = new Uint8Array(umemory.buffer);

				importObject[m][n] = umemory;
			}

			_execusr = new WebAssembly.Instance(_execmod, importObject);
			let uexports = _execusr.exports;
			//exp.instance = instance;
			console.log(_execusr);
			console.log(uexports);

			if (memoryExport) {
				umemory = uexports[memoryExport.name];
		    	umem = new DataView(umemory.buffer);
		    	uheapu8 = new Uint8Array(umemory.buffer);
				if (umemory.buffer instanceof SharedArrayBuffer) {
					_uvm_spacep = _exports.new_uvmspace();
					post_mmblkd_attach(umemory, _uvm_spacep);
				}
			}

			// TODO: entry-point from fork could use lwp_trampoline() as well.
			if (opts.fork_from && opts.fork_from.id) {
				let fork_from = opts.fork_from;
				uexports.in_fork.value = fork_from.id;
				if (fork_from.id == 1) {
					uexports.run_script.apply(null, fork_from.args);
					userfork = true;
				}
			}
		}

		if (!userfork) {
			let didret = false;
			while (!didret) {
				try {
					_exports.lwp_trampoline();
					didret = true;
				} catch (err) {
					if (err !== unwind4exec)
						throw err;
				}
			}
		}
		
		console.log("wasm32_fork_entrypoint() returned");
	}

	globalThis.addEventListener("message", function (evt) {
		//console.log(evt);
		let data = evt.data;
		if (data.cmd == "lwp_ctor") {
			spawn_thread(data);
		}
	});

})();
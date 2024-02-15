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

	// ERROR codes that is returned from lwp.js into wasm kernel
	const E2BIG = 7;
	const ENOMEM = 12;
	const EACCES = 13;
	const EINVAL = 22;
	const ENOEXEC = 8;
	const ENOENT = 2;

	let __kmodule;
	let __curlwp;
	let __stack_pointer;
	let __kesp;		// kernel-space stack-pointer (wasm global)
	let __uesp0;	// userspace stack-pointer (origin address, where argv/envp is stored etc.)
	let __uesp;		// userspace stack-pointer (wasm global)
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

	let _kernexp;
	let _userexp;
	let _exports;
	let _uinstance;
	let dylinked = false;
	const unwind4exec = {msg: "stack-unwinder for exec syscall"};

	// executing from kern_exec()
	let __in_posix_spawn = false;
	let _execbuf;
	let _execmod;
	let _execusr; // user executable instance.
	let _ustkptr;
	let _uheapbase;
	let _forkframe;
	let _meminfo;
	let _execfds;
	let _dyndl_exp;
	let _execmem;	// TODO: make sure we clear this around unwind4exec
	let _exectbl;
	let _execImportObject;
	let _includedTemp = false;

	// rtld (runtime dynamic loading/linking)
	let _rtld_inst;
	let _rtld_exports;

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
		if (uheapu8 === utmp || umemory.buffer.byteLength === uheapu8.buffer.byteLength)
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

	/**
	 * Returns the number of bytes the given Javascript string takes if encoded as a UTF8 byte array, EXCLUDING the null terminator byte.
	 */
	function lengthBytesUTF8(str) {
		let len = 0;
		for (let i = 0; i < str.length; ++i) {
			// Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code unit, not a Unicode code point of the character! So decode UTF16->UTF32->UTF8.
			// See http://unicode.org/faq/utf_bom.html#utf16-3
			let u = str.charCodeAt(i); // possibly a lead surrogate
			if (u >= 0xD800 && u <= 0xDFFF)
				u = 0x10000 + ((u & 0x3FF) << 10) | (str.charCodeAt(++i) & 0x3FF);
			if (u <= 0x7F)
				++len;
			else if (u <= 0x7FF)
				len += 2;
			else if (u <= 0xFFFF)
				len += 3;
			else 
				len += 4;
		}
		
		return len;
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
				let byte = kheapu8[kaddr + cnt];
				uheapu8[uaddr + cnt] = byte;
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


	// int kern.random_source(i32, i32, i32)
	function kern_random_source(buf, size, nread) {
		let bufsz = Math.min(size, 4096);
		let tmp = new Uint8Array(bufsz);
		try {
	    	crypto.getRandomValues(tmp);
		} catch (err) {
			return E2BIG;
		}
	    __check_kmem();
	    u8_memcpy(tmp, 0, bufsz, kheap32, buf);
		if (nread !== 0)
			kmemdata.setInt32(nread, bufsz, true);
	    return 0;
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

	// void kern.__copyout(i32, i32, i32, i32) (NOT USED?)
	function kern_console_log(buf, bufsz, flags, level) {
		let str = UTF8ArrayToString(kheapu8, buf, bufsz);
		if (str.startsWith("panic:") || str.indexOf(" ERROR ") != -1) {
			console.error(str);
		} else {
			console.log(str);
		}
	}

	// user imports

	// PHASE OUT
	function usr_stderr_stdout_write(fd, buf, bufsz) {
		let str = UTF8ArrayToString(uheapu8, buf, bufsz);
		str = str.trim();
		if (fd == 1) {
			console.warn("%c%s", "color: #ffc107;font-family:monospace; background-color: #181818; padding: 3px;", str);
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
		_kernexp.syscall_trap_handler(user_tf);
	}

	// PHASE OUT
	function usr_emscripten_get_heap_max() {
		return umemory.buffer.maxByteLength;
	}

	// PHASE OUT
	function usr_emscripten_get_heap_size() {
		return umemory.buffer.byteLength;
	}

	// PHASE OUT
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

	// makes shared memory mapped to global (same instance) memory-mapping thread (allows for copying and sharing to display-server)
	function post_mmblkd_attach(memory, vm_space_ptr) {
		if (vm_space_ptr === 0)
			throw new RangeError("mmblkd_attach is NULL");
		
		self.postMessage({
			cmd: "mmblkd_attach",
			memory: memory, 		// special role = "kmemory" key, normal memory = "memory" key
			meminfo_ptr: vm_space_ptr
		});
	}

	function klwp_spawn(l1, l2, stackptr, stacksz) {
		console.error("klwp_spawn l1 = %d l2 = %d stackptr = %d stacksz = %d", l1, l2, stackptr, stacksz);

		let name = "lwp @0x" + ((l2).toString(16).padStart(8, 0));
		let msg = {
			cmd: "lwp_spawn",
			kernel_module: __kmodule,
			kernel_memory: kmemory,
			__curlwp: l2,
			__stack_pointer: stackptr,
			__stack_size: stacksz,
			name: name,
		};
		// TODO: try to route execuation buffer loading to outside of the worker (Garbage collection?)
		if (__in_posix_spawn) {
			msg.user_execbuf = _execbuf;
			_execbuf = undefined;
		}
		self.postMessage(msg);
	}

	function wasm_sched_fork(td, childtd) {
		_forkframe = {};
		_forkframe.fork_from = _userexp.fork_from.value;
		_forkframe.fork_args = [_userexp.fork_arg.value];
		_userexp.fork_from.value = 0;
		_userexp.fork_arg.value = 0;
		let ustack_sz = _meminfo && Number.isInteger(_meminfo.default_stack_size) ? Math.ceil(_meminfo.default_stack_size / 4096) * 4096 : 65536;
		let ustack_ptr = _userexp.__libc_malloc(ustack_sz); // __libc_free
		if (ustack_ptr == 0) {
			throw new RangeError("ENOCORE");
		}
		let wqidx = Math.min(__stack_pointer.value / 4) - 1;
		Atomics.store(kheap32, wqidx, 0);
		self.postMessage({cmd: "wasm_sched_fork", args: [td, childtd], ustack: ustack_ptr, wqidx: wqidx});
		Atomics.wait(kheap32, wqidx, 0);
	}

	// PHASE OUT
	function kexecbuf_alloc(size) {
		if (_execbuf != undefined) {
			console.error("Invalid state; execbuf already exists");
			return;
		}
		_execbuf = new Uint8Array(size);
		return 0;
	}

	// PHASE OUT
	function kexecbuf_copy(kaddr, uaddr, size) {
		if (_execbuf == undefined) {
			console.error("Invalid state; no execbuf");
			return;
		}
		u8_memcpy(kheapu8, kaddr, size, _execbuf, uaddr);
		return 0;
	}

	// unwinds stack when execve_runproc completes (then jumps back in with lwp_trampoline)
	function kexec_finish() {
		if (_execbuf)
			libexec_prepare_wasm();
		// throw error to undwind call stack
		throw unwind4exec;
	}

	// /sbin/init specific

	/**
	 * Simple wrapper around posix_spawn to spawn a new process from user-space.
	 * 
	 * @note Caller must supply arg[0] which by convention is the invocation name (simple explaination: https://unix.stackexchange.com/a/315819)
	 * 
	 * @param {String} path Executable path to be passed to execv subrutine.
	 * @param {String[]} args A array of string or anything that is correctly casted to string.
	 * @param {Object} env Key-value-object that stores the enviroment variables to be set on the spawned process.
	 * @returns {integer} Returns the errno if error or 0 (zero) on sucess.
	 */
	function lwp_spawn_user(path, args, env) {
		let ret, rem;
		let ptr, bufsz, pathsz, argvoff, argbufsz, envpoff, envbufsz, argarrsz, envarrsz, bufp;
		let pid, execp, argc, argv, envp;
		let envarr = [];
		args = Array.isArray(args) ? args.slice() : [];

		// compute size needed in heap to spawn
		bufsz = 4; // pid_t

		pathsz = lengthBytesUTF8(path);
		bufsz += pathsz + 1;
		
		argvoff = alignUp(bufsz, 4);
		bufsz = argvoff;
		argbufsz = 0;
		argc = args.length;
		for (let i = 0; i < argc; i++) {
			let strval = args[i];
			if (typeof strval != "string") {
				strval = String(strval);
				args[i] = strval;
			}
			let strlen = lengthBytesUTF8(strval);
			argbufsz += strlen + 1;
		}

		argarrsz = ((argc + 1) * 4);
		bufsz += argarrsz; // all arg pointer + NULL terminated end
		envpoff = bufsz; 	// put **environ directly after arg[]
		bufsz += argbufsz;

		envbufsz = 0;
		for (let key in env) {
			let val = env[key];
			if (typeof val != "string") {
				val = String(val);
			}
			let pair = key + '=' + val;
			let strlen = lengthBytesUTF8(pair);
			envarr.push(pair);
			envbufsz += strlen + 1;
		}

		envarrsz = ((envarr.length + 1) * 4);
		bufsz += envarrsz; // pointer for all env string + NULL terminated end
		bufsz += envbufsz;

		ptr = _userexp.malloc(bufsz); // TODO: use calloc or zalloc
		if (ptr === 0) {
			console.error("ENOCORE");
			return -1;
		}

		// zero fill allocated memory
		uheapu8.fill(0, ptr, ptr + bufsz);

		pid = ptr;
		execp = ptr + 4;

		stringToUTF8Bytes(uheapu8, execp, path);

		argv = ptr + argvoff;
		bufp = argv + envarrsz + argarrsz;
		for (let i = 0; i < argc; i++) {
			let strval = args[i];
			let off = stringToUTF8Bytes(uheapu8, bufp, strval);
			umem.setUint32(argv + (i * 4), bufp, true);
			bufp = off + 1;
		}

		envp = ptr + envpoff;
		len = envarr.length;
		for (let i = 0; i < len; i++) {
			let strval = envarr[i];
			let off = stringToUTF8Bytes(uheapu8, bufp, strval);
			umem.setUint32(envp + (i * 4), bufp, true);
			bufp = off + 1;
		}
		
		__in_posix_spawn = true;
		ret = _userexp.__spawn_user_lwp(pid, execp, argc, argv, envp);
		__in_posix_spawn = false;
		if (ret !== 0) {
			console.error("__spawn_user_lwp failed ret = %d", ret);
		}

		_userexp.free(ptr);

		return ret;
	}

	function localTimeZone() {
		let str = new Date().toString();
		let idx = str.indexOf("GMT");
		return str.substring(idx, idx + 8);
	}

	// adhoc implementation calls this method once /sbin/init completes (this might change in the future)
	function init_did_finish() {
		let exec_env = {
			EXEC_TEST: 1234,
			SHELL: "/bin/bash",
			HOME: "/home/test-user",
			USERNAME: "test-user",
			LC_NAME: "en_US.UTF-8",
			LC_MONETARY: "en_US.UTF-8",
			LC_PAPER: "sv_SE.UTF-8",
			LANG: "en_US.UTF-8",
			PWD: "/home/test-user",
			USER: "test-user",
			LOGNAME: "test-user",
			PATH: "/opt/local/bin:/opt/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
			GNUSTEP_CONFIG_FILE: "/GNUstep.conf",
			GNUSTEP_TZ: localTimeZone(),
			FONTCONFIG_FILE: "/usr/lib/fontconfig/fonts.conf",
			// FC_DEBUG: FC_MATCH, // | FC_FONTSET | FC_OBJTYPES | FC_CONFIG;
			XDG_CACHE_HOME: "/home/test-user"
		};

		console.log("init_did_finish called");
		let exec_path = "/GnuStep/System/Applications/GWorkspace.app/GWorkspace";
		lwp_spawn_user(exec_path, [exec_path, "--test"], exec_env); // [], {})
	}


	
	// TODO: do this in a better way and phase this method out.
	function prepare_toplevel_user_importObject() {
		return {
			kern: {
				random_source: usr_random_source,
				panic_abort: kern_panic,
				usr_stderr_stdout_write: usr_stderr_stdout_write
			},
			env: {
				init_did_finish: init_did_finish,
			},
			sys: {
				syscall_trap: syscall_trap,
				sbrk: usbrk,
			},
			emscripten: {
				get_heap_size: usr_emscripten_get_heap_size,
				get_heap_max: usr_emscripten_get_heap_max,
				resize_heap: usr_emscripten_resize_heap,
			}
		};
	}

	// TODO: phase out the need of this method, it could be done similar how its done for dynamic-linking
	// a unification would be nice, since now there is two ways of initializing from buffer.
	function libexec_prepare_wasm() {
		
		if (_execbuf == undefined) {
			console.error("Invalid state; no execbuf");
			return;
		}

		if (_uvm_spacep) {
			_kernexp.deref_uvmspace(_uvm_spacep);
			_uvm_spacep = undefined;
		}

		_execmod = new WebAssembly.Module(_execbuf);
		_execbuf = undefined; // lose the buffer
		console.log(_execmod);
		console.log(WebAssembly.Module.exports(_execmod));
		console.log(WebAssembly.Module.imports(_execmod));

		// getting information on either imported or exported memory 
		let secu8;
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

		let importObject = prepare_toplevel_user_importObject();
		let sp = _meminfo ? _meminfo.default_stack_pointer : 0; 				// TODO: use __stackpointer + [argv + envv] == __heapbase as the stack size might not be enough otherwise.
		__uesp = new WebAssembly.Global({value: 'i32', mutable: true}, sp);
		__uesp0 = sp;
		//let exp = _kexp.initialize();
		importObject.kern.__stack_pointer = __uesp;

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
				_uvm_spacep = _kernexp.new_uvmspace();
				post_mmblkd_attach(umemory, _uvm_spacep);
			}

			importObject[m][n] = umemory;
		}

		let instance = new WebAssembly.Instance(_execmod, importObject);
		
		_userexp = instance.exports;
		//exp.instance = instance;
		console.log(instance);
		console.log(_userexp);
		_execusr = instance;

		if (memoryExport) {
			umemory = _userexp[memoryExport.name];
	    	umem = new DataView(umemory.buffer);
	    	uheapu8 = new Uint8Array(umemory.buffer);

			if (umemory.buffer instanceof SharedArrayBuffer) {
				_uvm_spacep = _kernexp.new_uvmspace();
				post_mmblkd_attach(umemory, _uvm_spacep);
			}
		}
	}


	function usbrk(arg) {
		console.warn("where is this called!");
		return 1024;
	}

	function kgettimespec64_clock(ts) {

		const now = Date.now();
	
		kmem.setBigInt64(ts, BigInt(now / 1000), true);					// seconds
		kmem.setInt32(ts + 8, ((now % 1000) * 1000 * 1000) | 0, true);	// nanoseconds
	}
	
	function kgettimespec64_monotomic(ts) {
	
		const now = performance.timeOrigin + performance.now();
		
		kmem.setBigInt64(ts, BigInt(now / 1000), true);					// seconds
		kmem.setInt32(ts + 8, ((now % 1000) * 1000 * 1000) | 0, true);	// nanoseconds
	}

	function wasm_clock_gettime(ts) {

		let now = performance.timeOrigin + performance.now();

		kmem.setInt32(ts, (now / 1000) | 0, true);					// seconds
		kmem.setInt32(ts + 4, ((now % 1000)*1000*1000) | 0, true);	// nanoseconds

		return 0;
	}

	// netbsd specific
	
	// proxies the programs main function to allow for multiple calling conventions.
	function exec_entrypoint(argc, argv, envp) {

		let ret;

		if (!_userexp) {
			_userexp = _execfds[1].instance.exports;
		}

		// __wasm_call_ctors must be called before any entry into binary.
		if (typeof _userexp.__wasm_call_ctors == "function") {
			_userexp.__wasm_call_ctors();
		}

		let mlen = _userexp.main.length;

		try {
			if (mlen == 2) {
				ret = _userexp.main(argc, argv);
			} else if (mlen == 3) {
				ret = _userexp.main(argc, argv, envp);
			} else if (mlen == 0) {
				ret = _userexp.main();
			}
		} catch (err) {
			console.error(err);
			throw err;
		}
		console.log("main ret = %d", ret);

		return ret;
	}

	function rtld_main_entrypoint(argc, argv, envp) {

		let ret;

		if (!_userexp) {
			_userexp = _execfds[1].instance.exports;
		}

		let mlen = _userexp.main.length;

		try {
			if (mlen == 2) {
				ret = _userexp.main(argc, argv);
			} else if (mlen == 3) {
				ret = _userexp.main(argc, argv, envp);
			} else if (mlen == 0) {
				ret = _userexp.main();
			}
		} catch (err) {
			console.error(err);
			throw err;
		}
		console.log("main ret = %d", ret);

		return ret;
	}

	function exec_rtld(fp, sp, base) {
		let fn = _exectbl.get(fp);
		ret = fn(sp, base);
		return;
	}

	function exec_start(__start, cleanup, psarg) {
		let fn = _exectbl.get(__start);
		fn(cleanup, psarg);
		return;
	}

	// empty placeholder for import in kernel (TODO: kexec_ioctl ? since its only used once in lwp0.js)
	function kspawn_blkdev_worker(rblkdev, init_cmd) {
		throw new Error("kspawn_blkdev_worker must be called on lwp0");
	}
	
	// void kern.cons_write(i32, i32, i32, i32)
	function kcons_write(strp, strsz, flags, level) {
		let outfn;
		let buf = kheapu8;
		if ((flags & (1 << 3)) != 0) {
			buf = uheapu8; // data has not been moved from user-space
		}

		let str = UTF8ArrayToString(buf, strp, strsz);

		if (level == 0 || level == 3) {
			outfn = console.log;
		} else if (level == 1 || level == 4) {
			outfn = console.warn;
		} else if (level == 2 || level == 5) {
			outfn = console.error;
		} else {
			outfn = console.log;
		}
		
		if (str.indexOf("] panic:") != -1 || str.indexOf(" ERROR ") != -1) {
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

	function createImportObject() {
		let importObject = {};
		if (!__uesp) {
			__uesp = new WebAssembly.Global({value: "i32", mutable: true}, 4096);	// 
		};
		importObject.env = {
			__stack_pointer: __uesp,
		};
		importObject.sys = {
			syscall_trap: syscall_trap
		};

		return importObject;
	}

	function temp_imports(obj) {
		obj.runtime = {
			__longjmp14: function (a1, a2) {
				return;
			},
			__setjmp14: function (a1) {
				return 0;
			},
		};

		if (!obj.env) {
			obj.env = {};
		}

		obj.env.pow = function(x, y) {
			return Math.pow(x, y);
		}
		obj.env.__floatunsitf = function(a1, a2) {}

		obj.env.__divtf3 = function (i1, i2, f1, f2, f3) {}

		obj.env.__isinfd = function (val) {
			return val == Infinity ? 1 : 0;
		}

		obj.env.log10 = function (x) {
			return Math.log10(x);
		}
		obj.env.log = function (x) {
			return Math.log(x);
		}

		obj.env.fmod = function (x, intpart) {
			let frac, abs = Math.floor(x);
			frac = x - abs;
			if (intpart !== 0)
				// TODO: set intpart double pointer
			return frac;
		}

		obj.env.modf = function (x, intpart) {
			let frac, abs = Math.floor(x);
			frac = x - abs;
			if (intpart !== 0)
				// TODO: set intpart double pointer
			return frac;
		}

		obj.env.cos = function (x) {
			return Math.cos(x);
		}
		obj.env.sin = function (x) {
			return Math.sin(x);
		}
		obj.env.tan = function (x) {
			return Math.tan(x);
		}
		obj.env.acos = function (x) {
			return Math.acos(x);
		}
		obj.env.asin = function (x) {
			return Math.asin(x);
		}
		obj.env.atan = function (x) {
			return Math.atan(x);
		}
		obj.env.atan2 = function (x, y) {
			return Math.atan2(x, y);
		}
		obj.env.hypot = function (x, y) {
			return Math.hypot(x, y);
		}
		obj.env.rexp = function (x, exponent) {
			return Math.exp(x);
		}
		obj.env.exp = function (x) {
			return Math.exp(x);
		}

		// alloca is a builtin and should be replaced at link/compile time to use __stack_point
		obj.__builtin = {
			alloca: function(a1) {
				kern_panic();
				return 0;
			}
		};

		// these are part of libc which is either proxied on purpose or have not been included yet.
		obj.env.sbrk = kern_panic;
		obj.env.brk = kern_panic;
		obj.env.inet_pton = function(a1, a2, a3) {
			return 0;
		}
		obj.env.inet_network = function(a1) {
			return 0;
		}
		obj.env.inet_aton = function (a1, a2) {
			return 0;
		}
		obj.env.inet_ntop = function (a1, a2, a3, a4) {
			return 0;
		}
		obj.env.inet_nsap_ntoa = function(a1, a2, a3) {
			return 0;
		}
		obj.env.clnttcp_create = function (a1, a2, a3, a4, a5, a6) {
			return 0;
		}
		obj.env.clntudp_bufcreate = function (a1, a2, a3, a4, a5, a6, a7) {
			return 0;
		}
		obj.env.clntudp_create = function (a1, a2, a3, a4, a5) {
			return 0;
		}
		obj.env.__chk_fail = function () {
			return;
		}

		// needed by libobjc but should be done using wasm-eh
		obj.env.__cxa_end_catch = function() {};

		obj.cxxabi = {
			_Unwind_RaiseException: function (a1) {
				return 0;
			},
			_Unwind_GetLanguageSpecificData: function(a1) {
				return 0;
			},
			_Unwind_SetIP: function (a1, a2) {
				return;
			},
			_Unwind_SetGR: function(a1, a2, a3) {
				return;
			},
			_Unwind_Resume_or_Rethrow: function(a1) {
				return 0;
			},
			_Unwind_GetRegionStart: function (a1) {
				return 0;
			},
			_Unwind_GetIP: function (a1) {
				return 0;
			},
			_Unwind_GetTextRelBase: function(a1) {
				return 0;
			},
			_Unwind_GetDataRelBase: function(a1) {
				return 0;
			},
			__cxa_free_exception: function(a1) {
				return;
			}
		}

		// weak bindings for dlfcn
		obj.dlfcn = {
			//__dlsym: _dyndl_exp.exports.__dlsym,
			__dlsym: function (a1, a2, a3) {
				console.error("called dlfcn.__dlsym");
				if (_rtld_exports)
					return _rtld_exports.__dlsym(a1, a2, a3);
				return 0;
			},
			__dlclose: function (a1, a2) {
				console.error("called dlfcn.__dlclose");
				if (_rtld_exports)
					return _rtld_exports.__dlclose(a1, a2);
				return 0;
			},
			__dlopen: function (a1, a2, a3) {
				console.error("called dlfcn.__dlopen");
				if (_rtld_exports)
					return _rtld_exports.__dlopen(a1, a2, a3);
				return 0;
			},
			__dlerror: function (a1) {
				console.error("called dlfcn.__dlerror");
				if (_rtld_exports)
					return _rtld_exports.__dlerror(a1);
				return 0;
			},
			__dladdr: function (a1, a2, a3) {
				console.error("called dlfcn.__dladdr");
				if (_rtld_exports)
					return _rtld_exports.__dladdr(a1, a2, a3);
				return 0;
			},
			__dladdr1: function (a1, a2, a3, a4, a5) {
				console.error("called dlfcn.__dladdr1");
				if (_rtld_exports)
					return _rtld_exports.__dladdr1(a1, a2, a3, a4, a5);
				return 0;
			},
			__dlinfo: function (a1, a2, a3, a4, a5) {
				console.error("called dlfcn.__dlinfo");
				if (_rtld_exports)
					return _rtld_exports.__dlinfo(a1, a2, a3, a4, a5);
				return 0;
			},
			main_entrypoint: rtld_main_entrypoint,
			__ld_ioctl: function (cmd, data) {

				if (!_rtld_exports)
					throw new Error("INVALID_STATE");

				switch (cmd) {
					case 12:
						_rtld_exports.call_pre_init_array();
						return 0;
					case 13:
						_rtld_exports.call_init_array();
						return 0;
					case 14:
						_rtld_exports.setup_fnit_array();
						return 0;
				}
				throw new Error("INVALID_CMD");
			},
		};

		// marker for indirect objc calls (should be handled by linker)
		obj.objc_callsite = {
			objc_indirect_marker: function (fp) {
				return; // do nothing
			}
		}

		// due do same headers as kernel (TODO: should be its own import)
		if (!obj.kern) {
			obj.kern = {};
		}

		// TODO: libc should define its on way to handle abort
		obj.kern.__panic_abort = kern_panic;
		obj.kern.panic_abort = kern_panic;
		obj.kern.usr_stderr_stdout_write = usr_stderr_stdout_write;

		// gnustep-back (these two are somehow not found in pixman)
		obj.env.pixman_rasterize_trapezoid = function(a1, a2, a3, a4, a5) {
			kern_panic();
			return;
		}
		obj.env.pixman_add_triangles = function(a1, a2, a3, a4, a5) {
			kern_panic();
			return;
		}

		// gnustep-back (old bindings)
		obj.wmaker = {
			assign_framebuffer: function(a1, a2) {
				return;
			},
			get_screen_size: function() {
				return 0;
			},
			increment_window_id: function() {
				return 0;
			},
			create_window: function (a1) {
				return 0;
			},
			set_window_level: function (a1, a2) {
				return;
			},
			place_window: function (a1, a2, a3, a4, a5) {
				return;
			},
			order_window: function (a1, a2, a3) {
				return;
			},
			set_window_title: function (a1, a2) {
				return;
			},
			set_input_state: function (a1, a2) {
				return;
			},
			finalize_surface: function (a1) {
				return;
			},
			flush_dirty_rect: function (a1, a2, a3, a4, a5) {
				return;
			},
			set_cursor: function (a1, a2) {
				return;
			},
			hide_cursor: function () {
				return;
			},
			show_cursor: function () {
				return;
			}
		};
	}

	// unlike kexec_ioctl, rtld is always in user-space memory
	function rtld_exec_ioctl(cmd, argp) {
		switch (cmd) {
			case 552: { 				// EXEC_IOCTL_MKBUF
				if (argp == 0)
					return EINVAL;
				let fd = -1;
				let buf, bufsz = umem.getUint32(argp + 4, true);
				try {
					buf = new Uint8Array(bufsz);
				} catch (err) {
					return ENOMEM;
				}
				
				if (!_execfds)
					_execfds = {};
				for (let i = 1;  i < 4096; i++) {
					if (_execfds.hasOwnProperty(i) == false) {
						fd = i;
						_execfds[fd] = {buf: buf};
						break;
					}
				}

				umem.setInt32(argp, fd, true);
				return 0;
			}
			case 569: {					// RTLD_IOCTL_EXECBUF_MAP_FROM_MEMORY
				if (argp == 0)
					return EINVAL;
				let desc, fd = -1;
				let bufsz = kmem.getUint32(argp + 4, true);
				let cpcnt = kmem.getUint32(argp + 8, true);
				let argsp = kmem.getUint32(argp + 12, true);

				if (!_execfds)
					_execfds = {};
				for (let i = 1;  i < 4096; i++) {
					if (_execfds.hasOwnProperty(i) == false) {
						fd = i;
						desc = {};
						_execfds[fd] = desc;
						break;
					}
				}

				if (argsp === 0 || cpcnt === 0 || fd == -1) {
					if (fd != -1)
						delete _execfds[fd];
					return EINVAL;
				}

				let dstbuf = new Uint8Array(bufsz);
				desc.buf = buf;
				
				let src, dst, len;
				for (let i = 0; i < cpcnt; i++) {
					dst = umem.getUint32(argsp + 0, true);
					src = umem.getUint32(argsp + 4, true);
					len = umem.getUint32(argsp + 8, true);
					u8_memcpy(uheapu8, src, len, dstbuf, dst);
					argsp += 12;
				}
				
				umem.setInt32(argp, fd, true);

				return 0;
			}
			case 557: { 				// EXEC_IOCTL_COMPILE
				if (argp == 0)
					return EINVAL;
				let module, instance, phase, __dso_glob;
				let fd = umem.getInt32(argp, true);
				let flags = umem.getInt32(argp + 4, true);
				let __dso_handle = umem.getUint32(argp + 8, true);
				let __tls_handle = umem.getUint32(argp + 12, true);
				let expname = umem.getUint32(argp + 16, true);
				let expnamesz = umem.getUint8(argp + 20, true);
				// 21 is errphase
				// 24 is errno
				let errmsgsz = umem.getUint32(argp + 28, true);
				let errmsgp = umem.getUint32(argp + 32, true);
				
				if (!_execfds.hasOwnProperty(fd))
					return EINVAL;
				let desc = _execfds[fd];
				let buf = desc.buf;
				if (!_includedTemp) {
					temp_imports(_execImportObject);
					_includedTemp = true;
				}
				try {
					phase = 1;
					module = new WebAssembly.Module(buf);
					let importObject = Object.assign({}, _execImportObject);
					let newsys = Object.assign({}, importObject.sys);
					__dso_glob = new WebAssembly.Global({value: "i32", mutable: false}, __dso_handle);
					newsys.__dso_handle = __dso_glob;
					importObject.sys = newsys;
					phase = 2;
					instance = new WebAssembly.Instance(module, importObject);
					desc.instance = instance;
					delete desc.buf;

				} catch (err) {
					// Example errors:
					// CompileError: WebAssembly.Module(): expected 5006784 bytes, fell off end @+78248
					// CompileError: WebAssembly.Module(): length overflow while decoding functions count @+34986
					// CompileError: WebAssembly.Module(): length overflow while decoding body size @+70393
					// CompileError: WebAssembly.Module(): illegal flag value 116. Must be 0, 1, or 2 @+17267
					console.error(err);
					umem.setInt32(argp + 24, ENOEXEC, true);
					let str = err.message;
					if (errmsgp !== 0) {
						let strlen = lengthBytesUTF8(str);
						if (strlen < errmsgsz) {
							stringToUTF8Bytes(kheapu8, errmsgp, str);
							umem.setUint32(argp + 28, strlen, true);
						} else {
							str = str.substring(0, errmsgsz);
							strlen = lengthBytesUTF8(str);
							stringToUTF8Bytes(kheapu8, errmsgp, str);
							umem.setUint32(argp + 28, strlen, true);
						}
					}
					
					return ENOEXEC;
				}

				// if export name is specified its added to _execImportObject for subsequent compile and instanciation.
				if (expname !== 0 && expnamesz > 0) {
					expname = UTF8ArrayToString(kheapu8, expname, expnamesz);						
					if (_execImportObject.hasOwnProperty(expname)) {
						let dst = _execImportObject[expname];
						let src = instance.exports;
						for (let p in src) {
							dst[p] = src[p];
						}
					} else {
						let exp = Object.assign({}, instance.exports);
						_execImportObject[expname] = exp;
					}
				}

				// runs __wasm_ctor_dylib_tbl if exported, this places the element-segments at specified locations.
				if (instance.exports.__wasm_ctor_dylib_tbl) {
					instance.exports.__wasm_ctor_dylib_tbl();
				}
				
				// wait with __wasm_call_ctors until __stack_pointer has been set.

				return 0;
			}
		}
	}

	function kexec_ioctl(cmd, argp) {
		switch (cmd) {

			case 512: {	// get user stack-pointer
				if (argp !== 0) {
					kmem.setUint32(argp, __uesp.value, true);
				}
				break;
			}

			case 513: { // set user stack-pointer
				__uesp.value = argp;
				break;
			}
			case 537: {
				console.warn("kmem_alloc() large chunk %d", argp);
				break;
			}
			case 545: {
				if (argp === 0)
					return EINVAL;
				if (!__uesp)
					return ENOENT;
				kmem.setUint32(argp, __uesp.value, true);
				return 0;
			}
			// this one is similar to 548 but works with ld (dynamic-linking)
			case 546: {
				if (argp === 0)
					return EINVAL;
				let growsz, size = -1;
				growsz = kmem.getUint32(argp, true);
				if (growsz == 0)
					return EINVAL;
				try {
					size = _execmem.grow(growsz);
				} catch (err) {
					console.error(err);
					debugger;
					return ENOMEM;
				}
				if (size == -1) {
					debugger;
					return ENOMEM;
				} else {
					let sp = (size + growsz) * 65536;
					__uesp.value = sp;
					__uesp0 = sp;
					kmem.setUint32(argp, sp, true);
					// since we did a resize update r/w buf
					umemory = _execmem;
					uheapu8 = new Uint8Array(umemory.buffer);
					return 0;
				}
			}
			// (old way) compile _usrexec buffer
			case 547: {
				try {
					libexec_prepare_wasm();
				} catch (err) {
					debugger;
					throw err;
				}
				break;
			}
			// (old way) tries to allocate range of memory for userspace stack
			case 548: {
				let growsz, size = -1;
				growsz = argp;
				try {
					size = umemory.grow(1);
				} catch (err) {
					console.error(err);
					debugger;
					return -ENOMEM;
				}
				if (size == -1) {
					debugger;
					return -ENOMEM;
				} else {
					let sp = (size + 1) * 65536;
					__uesp.value = sp;
					__uesp0 = sp;
					return sp;
				}
			}
			// like alloca but must be done when stack-pointer is at start position.
			case 549: {
				let sp =  __uesp.value;
				sp = (sp - argp);
				__uesp.value = sp;
				return sp;
			}
			case 552: { 				// EXEC_IOCTL_MKBUF
				if (argp == 0)
					return EINVAL;
				let fd = -1;
				let buf, bufsz = kmem.getUint32(argp + 4, true);
				try {
					buf = new Uint8Array(bufsz);
				} catch (err) {
					return ENOMEM;
				}
				
				if (!_execfds)
					_execfds = {};
				for (let i = 1;  i < 4096; i++) {
					if (_execfds.hasOwnProperty(i) == false) {
						fd = i;
						_execfds[fd] = {buf: buf};
						break;
					}
				}

				kmem.setInt32(argp, fd, true);
				return 0;
			}
			case 553: { 				// EXEC_IOCTL_WRBUF
				if (argp == 0)
					return EINVAL;
				let fd = kmem.getInt32(argp, true);
				let src = kmem.getUint32(argp + 4, true);	// kmem
				let dst = kmem.getUint32(argp + 8, true);
				let len = kmem.getUint32(argp + 12, true);
				if (!_execfds.hasOwnProperty(fd))
					return EINVAL;
				let buf = _execfds[fd].buf;
				u8_memcpy(kheapu8, src, len, buf, dst);
				return 0;
			}
			case 554: { 				// EXEC_IOCTL_RDBUF
				if (argp == 0)
					return EINVAL;
				let fd = kmem.getInt32(argp, true);
				let src = kmem.getUint32(argp + 4, true);
				let dst = kmem.getUint32(argp + 8, true);	// kmem
				let len = kmem.getUint32(argp + 12, true);
				if (!_execfds.hasOwnProperty(fd))
					return EINVAL;
				let buf = _execfds[fd].buf;
				u8_memcpy(buf, src, len, kheapu8, dst);
				return 0;
			}
			case 555: { 				// EXEC_IOCTL_CP_BUF_TO_MEM
				if (argp == 0)
					return EINVAL;
				if (!_execmem) {
					throw new Error("INVALID_STATE");
				}

				let fd = kmem.getInt32(argp, true);
				let src = kmem.getUint32(argp + 4, true);	
				let dst = kmem.getUint32(argp + 8, true);	// kmem
				let len = kmem.getUint32(argp + 12, true); 	// TODO: assert locations
				if (!_execfds.hasOwnProperty(fd))
					return EINVAL;
				let buf = _execfds[fd].buf;
				u8_memcpy(buf, src, len, uheapu8, dst);
				return 0;
			}
			case 556: { 				// EXEC_IOCTL_CP_KMEM_TO_UMEM
				if (argp == 0)
					return EINVAL;
				if (!_execmem) {
					throw new Error("INVALID_STATE");
				}

				let fd = kmem.getInt32(argp, true);
				let src = kmem.getUint32(argp + 4, true);	// kmem
				let dst = kmem.getUint32(argp + 8, true);	
				let len = kmem.getUint32(argp + 12, true); 	// TODO: assert locations
				u8_memcpy(kheapu8, src, len, uheapu8, dst);
				return 0;
			}
			case 557: { 				// EXEC_IOCTL_COMPILE
				if (argp == 0)
					return EINVAL;
				let module, instance, phase, __dso_glob;
				let fd = kmem.getInt32(argp, true);
				let flags = kmem.getInt32(argp + 4, true);
				let __dso_handle = kmem.getUint32(argp + 8, true);
				let __tls_handle = kmem.getUint32(argp + 12, true);
				let expname = kmem.getUint32(argp + 16, true);
				let expnamesz = kmem.getUint8(argp + 20, true);
				// 21 is errphase
				// 24 is errno
				let errmsgsz = kmem.getUint32(argp + 28, true);
				let errmsgp = kmem.getUint32(argp + 32, true);
				
				if (!_execfds.hasOwnProperty(fd))
					return EINVAL;
				let desc = _execfds[fd];
				let buf = desc.buf;
				if (!_includedTemp) {
					temp_imports(_execImportObject);
					_includedTemp = true;
				}
				try {
					phase = 1;
					module = new WebAssembly.Module(buf);
					let importObject = Object.assign({}, _execImportObject);
					let newsys = Object.assign({}, importObject.sys);
					__dso_glob = new WebAssembly.Global({value: "i32", mutable: false}, __dso_handle);
					newsys.__dso_handle = __dso_glob;
					importObject.sys = newsys;
					phase = 2;
					instance = new WebAssembly.Instance(module, importObject);
					desc.instance = instance;
					delete desc.buf;

				} catch (err) {
					// Example errors:
					// CompileError: WebAssembly.Module(): expected 5006784 bytes, fell off end @+78248
					// CompileError: WebAssembly.Module(): length overflow while decoding functions count @+34986
					// CompileError: WebAssembly.Module(): length overflow while decoding body size @+70393
					console.error(err);
					kmem.setInt32(argp + 24, ENOEXEC, true);
					let str = err.message;
					if (errmsgp !== 0) {
						let strlen = lengthBytesUTF8(str);
						if (strlen < errmsgsz) {
							stringToUTF8Bytes(kheapu8, errmsgp, str);
							kmem.setUint32(argp + 28, strlen, true);
						} else {
							str = str.substring(0, errmsgsz);
							strlen = lengthBytesUTF8(str);
							stringToUTF8Bytes(kheapu8, errmsgp, str);
							kmem.setUint32(argp + 28, strlen, true);
						}
					}
					
					return ENOEXEC;
				}

				// if export name is specified its added to _execImportObject for subsequent compile and instanciation.
				if (expname !== 0 && expnamesz > 0) {
					expname = UTF8ArrayToString(kheapu8, expname, expnamesz);						
					if (_execImportObject.hasOwnProperty(expname)) {
						let dst = _execImportObject[expname];
						let src = instance.exports;
						for (let p in src) {
							dst[p] = src[p];
						}
					} else {
						let exp = Object.assign({}, instance.exports);
						_execImportObject[expname] = exp;
					}
				}

				// runs __wasm_ctor_dylib_tbl if exported, this places the element-segments at specified locations.
				if (instance.exports.__wasm_ctor_dylib_tbl) {
					instance.exports.__wasm_ctor_dylib_tbl();
				}
				
				// wait with __wasm_call_ctors until __stack_pointer has been set.

				return 0;
			}
			case 558: { 				// EXEC_IOCTL_MK_UMEM
				if (argp == 0)
					return EINVAL;
				if (_execmem)
					throw new Error("INVALID_STATE");
				let desc;
				let min = kmem.getInt32(argp, true);
				let max = kmem.getInt32(argp + 4, true);
				let shared = kmem.getUint8(argp + 8) == 1;
				let index = kmem.getUint8(argp + 9);
				let mem;
				// TODO: make property assignment dynamic.
				// TODO: how to handle multiple memories in the future?
				let module = "env";
				let name = "__linear_memory";
				desc = {};
				if (min !== -1) {
					desc.initial = min;
				}
				if (max !== -1) {
					desc.maximum = max;
				}
				if (index == 64) {
					// https://webassembly.github.io/memory64/js-api/index.html#memories
					desc.index = "i64";
				}
				desc.shared = shared;
				try {
					mem = new WebAssembly.Memory(desc);
					_execmem = mem;
					uheapu8 = new Uint8Array(_execmem.buffer);
					if (!umemory)
						umemory = mem;
				} catch (err) {
					console.error(err);
					return ENOMEM;
				}

				if (!_execImportObject)
					_execImportObject = createImportObject();
				
				if (_execImportObject.hasOwnProperty(module) == false) {
					_execImportObject[module] = {};
				}

				_execImportObject[module][name] = mem;
				
				return 0;
			}
			case 559: { 				// EXEC_IOCTL_UMEM_GROW
				if (argp == 0)
					return EINVAL;
				if (!_execmem) {
					return ENOENT;
				}
				let ret, sz = kmem.getInt32(argp, true);
				try {
					ret = _execmem.grow(sz);
					uheapu8 = new Uint8Array(_execmem.buffer);
				} catch (err) {
					console.error(err);
					return ENOMEM;
				}
				
				kmem.setInt32(argp + 4, ret, true);
				return 0;
			}
			case 560: { 				// EXEC_IOCTL_RLOC_LEB
				if (argp == 0)
					return EINVAL;
				
				let fd = kmem.getInt32(argp, true);
				let count = kmem.getUint32(argp + 4, true);
				let lebsz = kmem.getUint32(argp + 8, true);	
				let datap = kmem.getUint32(argp + 12, true);
				
				if (lebsz != 5) {
					return ENOEXEC;
				}

				if (!_execfds.hasOwnProperty(fd))
					return EINVAL;

				let ptr = datap;
				let buf = _execfds[fd].buf;
				for (let i = 0; i < count; i++) {
					let src, dst = kmem.getUint32(ptr, true);
					src = ptr + 4;
					// copying leb
					buf[dst] = kheapu8[src];
					buf[++dst] = kheapu8[++src];
					buf[++dst] = kheapu8[++src];
					buf[++dst] = kheapu8[++src];
					buf[++dst] = kheapu8[++src];
					ptr += 9;
				}
				
				return 0;
			}
			case 561: { 				// EXEC_IOCTL_RLOC_I32
				if (argp == 0)
					return EINVAL;
				
				let buf, fd = kmem.getInt32(argp, true);
				let count = kmem.getUint32(argp + 4, true);
				let datap = kmem.getUint32(argp + 8, true);

				if (fd === -1) { // -1 direct reloc to user-space memory (after it has been moved)
					buf = uheapu8;
				} else {
					if (!_execfds.hasOwnProperty(fd))
						return EINVAL;
						buf = _execfds[fd].buf;
				}

				let ptr = datap;
				for (let i = 0; i < count; i++) {
					let src, dst = kmem.getUint32(ptr, true);
					src = ptr + 4;
					// copying i32 TODO: we could use getUint32() and setUint32() here
					buf[dst] = kheapu8[src];
					buf[++dst] = kheapu8[++src];
					buf[++dst] = kheapu8[++src];
					buf[++dst] = kheapu8[++src];
					ptr += 8;
				}
				
				return 0;
			}
			case 563: { 				// EXEC_IOCTL_RUN_MODULE_AS_DYN_LD
				if (argp == 0)
					return EINVAL;
				let fd = kmem.getInt32(argp, true);
				let strsz = kmem.getUint32(argp + 8, true);
				if (!_execfds.hasOwnProperty(fd))
					return EINVAL;
				let importObject;
				let mod, desc = _execfds[fd];
				let buf = desc.buf;
				delete desc.buf;
				desc._ctors_done = true;
				_rtld_inst = undefined;
				_rtld_exports = undefined;
				try {
					mod = new WebAssembly.Module(buf);
					importObject = createImportObject();
					importObject.rtld = {
						cons_write: kcons_write,
						rtld_exec_ioctl: rtld_exec_ioctl
					};
					importObject.kern = {
						panic_abort: kern_panic
					};
					importObject.env.__linear_memory = _execmem;
					importObject.env.__indirect_function_table = _exectbl;
					importObject.env.__cxa_atexit = function (a, b, c) { return 0; }

					_dyndl_exp = new WebAssembly.Instance(mod, importObject);
					_rtld_inst = _dyndl_exp;
					_rtld_exports = _rtld_inst.exports;
					console.log(_dyndl_exp);
				} catch (err) {
					console.error(err);
					kmem.setInt32(argp + 4, 1, true);
					let str = err.message;
					let strlen = lengthBytesUTF8(str);
					if (strlen < strsz) {
						stringToUTF8Bytes(kheapu8, argp + 12, str);
						kmem.setUint32(argp + 8, strlen, true);
					} else {
						str = str.substring(0, strsz);
						strlen = lengthBytesUTF8(str);
						stringToUTF8Bytes(kheapu8, argp + 12, str);
						kmem.setUint32(argp + 8, strlen, true);
					}
					
					return ENOEXEC;
				}

				// runs __wasm_ctor_dylib_tbl if exported, this places the element-segments at specified locations.
				if (_rtld_exports) {
					if (_rtld_exports.__wasm_ctor_dylib_tbl)
						_rtld_exports.__wasm_ctor_dylib_tbl();
				}

				return 0;
			}
			case 564: { 				// EXEC_IOCTL_UTBL_MAKE
				if (argp == 0)
					return EINVAL;
				if (_exectbl) {
					return EINVAL;
				}
				let tbl, desc;
				let initial = kmem.getInt32(argp, true);
				let maximum = kmem.getInt32(argp + 4, true);
				let reftype = kmem.getUint8(argp + 8, true);
				//let module = kmem.getUint32(argp + 12, true);
				//let name = kmem.getUint32(argp + 16, true);
				let module = "env";
				let name = "__indirect_function_table";

				if (reftype == 0x67) {
					reftype = "externref"
				} else if (reftype == 0x70) {
					reftype = "anyfunc";
				} else {
					return EINVAL;
				}

				desc = {initial: initial, element: reftype};
				
				if (maximum != -1) {
					desc.maximum = maximum;
				}
				
				try {
					tbl = new WebAssembly.Table(desc);
					_exectbl = tbl;
				} catch (err) {
					console.error(err);
					return EINVAL;
				}

				if (!_execImportObject)
					_execImportObject = createImportObject();

				if (_execImportObject.hasOwnProperty(module) == false) {
					_execImportObject[module] = {};
				}

				_execImportObject[module][name] = tbl;
				
				return 0;
			}
			case 565: { 				// EXEC_IOCTL_UTBL_GROW
				if (argp == 0)
					return EINVAL;
				if (!_exectbl) {
					return ENOENT;
				}
				let ret, sz = kmem.getInt32(argp, true);
				try {
					ret = _exectbl.grow(sz);
				} catch (err) {
					console.error(err);
					return ENOMEM;
				}
				
				kmem.setInt32(argp + 4, ret, true);
				return 0;
			}
			case 566: { 				// EXEC_IOCTL_DYNLD_DLSYM_EARLY
				if (argp == 0)
					return EINVAL;
				if (!_dyndl_exp) {
					return ENOENT;
				}
				let ret, namep, dynsym_start = kmem.getUint32(argp, true);
				let dynsym_end = kmem.getUint32(argp + 4, true);
				let sym_namesz = kmem.getUint32(argp + 8, true);
				let sym_namep = kmem.getUint32(argp + 12, true);
				let sym_type = kmem.getUint8(argp + 16, true);

				/*if (dynsym_start == 2720180 && sym_namesz == 4 && UTF8ArrayToString(kheapu8, sym_namep, 4) == "__sF") {
					debugger;
				} else if (sym_namesz == 10 && UTF8ArrayToString(kheapu8, sym_namep, 10) == "NSFileSize") {
					debugger;
				}*/

				// TODO: use dedicated memory range in user-space
				namep = 2048;
				u8_memcpy(kheapu8, sym_namep, sym_namesz, uheapu8, namep);

				try {
					ret = _dyndl_exp.exports.__dlsym_early(dynsym_start, dynsym_end, sym_namesz, namep, sym_type);
				} catch (err) {
					console.error(err);
					return ENOENT
				}

				if (ret < 0) {
					return -(ret);
				} else {
					kmem.setInt32(argp + 20, ret, true); // symbol_addr
					return 0;
				}
			}
			case 567: {					// EXEC_IOCTL_BUF_REMAP
				if (argp == 0)
					return EINVAL;
				let fd = kmem.getInt32(argp, true);
				let newsz = kmem.getUint32(argp + 4, true);
				let cpcnt = kmem.getUint32(argp + 8, true);
				let argsp = kmem.getUint32(argp + 12, true);
				if (argsp === 0 || cpcnt === 0 || !_execfds.hasOwnProperty(fd))
					return EINVAL;
				let src, dst, len;
				let srcbuf = _execfds[fd].buf;
				let dstbuf = new Uint8Array(newsz);
				for (let i = 0; i < cpcnt; i++) {
					dst = kmem.getUint32(argsp + 0, true);
					src = kmem.getUint32(argsp + 4, true);
					len = kmem.getUint32(argsp + 8, true);
					u8_memcpy(srcbuf, src, len, dstbuf, dst);
					argsp += 12;
				}
				
				_execfds[fd].buf = dstbuf;

				return 0;
			}
			case 570: {					// EXEC_IOCTL_RUN_RTLD_INIT
				if (!_rtld_exports || typeof _rtld_exports.__rtld_init != "function")
					return ENOENT;
				_rtld_exports.__rtld_init(); // todo use __wasm_ctor for rtld..
				return 0;
			}
			/*
			case 568: {					// EXEC_IOCTL_EXEC_CTORS
				if (_dyndl_exp) {

					// TODO: should be done by a call into _dyndl_exp which has the .init_array mapped
					// TODO: should also respect the dependancy chain.

					const OBJCV2_LOAD_SYM = ".objcv2_load_function";
					
					for (let fd in _execfds) {
						let desc = _execfds[fd];
						if (desc._ctors_done === true)
							continue;
						let instance = desc.instance;
						let exp = instance.exports;
						if (typeof exp.__wasm_call_ctors == "function") {
							exp.__wasm_call_ctors();
						} else if (typeof exp[OBJCV2_LOAD_SYM] == "function") {
							let fn = exp[OBJCV2_LOAD_SYM];
							fn();
						}
						desc._ctors_done = true;
					}

					return 0;
				} else if (_userexp) {



					return 0;
				} else {
					return EINVAL;
				}
			}*/
			

		}
	}

	/*function syscall_trap() {
		//let stackptr = _uinstance.
		console.log(_uinstance);
		// copy in; copy value of userland td_sa (110624) to kernel-land td->td_sa
		u8_memcpy(uheapu8, 110624, 80, kheapu8, __curlwp + 672);
		_kernexp.do_syscall_handler();
		// copy out; copy value of kernel-land &td->td_frame to userland td_frame
		let ptr = kmem.getUint32(__curlwp + 840);
		// offset of td->td_sa 672
		// offset of td->td_frame 800
	}*/

	_kexp.syscall_trap = syscall_trap;
	_kexp.random_source = kern_random_source;

	// weak bindings since its not loaded right away..
	function __dlsym_early(a1, a2, a3, a4, a5, a6) {
		if (!_dyndl_inst) {
			return ENOEXEC;
		}

		return _dyndl_inst.exports.__dlsym_early(a1, a2, a3, a4, a5, a6);
	}

	// main entry-point for setting up a new backing Worker for a lwp, this is used both for kernel + user and kernel-only spawning.
	function spawn_thread(opts) {
		kmemory = opts.kernel_memory;
		kmembuf = kmemory.buffer;
		kheap32 = new Int32Array(kmembuf);
		kheapu8 = new Uint8Array(kmembuf);
		kmem = new DataView(kmembuf);
		__curlwp = opts.__curlwp;
		__kesp = new WebAssembly.Global({value: 'i32', mutable: true}, opts.__stack_pointer);

		let userfork = false;
		
		// kernel module (executable instance)
		let importObject = {
			env: {
				memory: kmemory,
			},
			kern: {
				__curlwp: new WebAssembly.Global({value: 'i32', mutable: false}, opts.__curlwp),
				__stack_pointer: __kesp,
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
				//wasm_thread_alloc: wasm_thread_alloc,
				//wasm_sched_add: wasm_sched_add,
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
				gettimespec64_clock: kgettimespec64_clock,
				gettimespec64_monotomic: kgettimespec64_monotomic,
				// kern_exec
				execbuf_alloc: kexecbuf_alloc,
				execbuf_copy: kexecbuf_copy,
				exec_finish: kexec_finish,			// called from wasm to replace the current user execuble with the one loaded into _execbuf
				exec_entrypoint: exec_entrypoint,
				exec_rtld: exec_rtld,
				exec_start: exec_start,
				exec_ioctl: kexec_ioctl,
			},
			dlfcn: {
				__dlsym_early: __dlsym_early
			},
			emscripten: {
				memcpy_big: emscripten_memcpy_big,
			}
		};

		__curlwp = opts.__curlwp;
		__kmodule = opts.kernel_module;
		//__stack_pointer = importObject.kern.__stack_pointer;
		let instance = new WebAssembly.Instance(opts.kernel_module, importObject);
		_kernexp = instance.exports;

		// user module (executable instance)
		console.log("spawned %s thread", globalThis.name);
		if (globalThis.name == "lwp @0x007a2c80") {
			debugger;
		}

		let user_module;

		if (opts.user_module) {
			user_module = opts.user_module;
		} else if (opts.user_execbuf) {
			user_module = new WebAssembly.Module(opts.user_execbuf);
		}

		if (user_module) {
			_execmod = user_module;
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

			// TODO: use __stackpointer + [argv + envv] == __heapbase as the stack size might not be enough otherwise.
			let _ustack_ptr = 0;
			if (Number.isInteger(opts.ustack)) {
				_ustack_ptr = opts.ustack
			}

			// TODO: add a if dynlink == true construct indirect table here!

			if (instance === null) {

				__uesp = new WebAssembly.Global({value: 'i32', mutable: true}, _ustack_ptr);
				//let exp = _kexp.initialize();
				let importObject = prepare_toplevel_user_importObject();

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
							_uvm_spacep = _kernexp.new_uvmspace();
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
			} else {
				_execusr = instance;
			}
			let uexports = _execusr.exports;
			//exp.instance = instance;
			console.log(_execusr);
			console.log(uexports);

			if (memoryExport) {
				umemory = uexports[memoryExport.name];
		    	umem = new DataView(umemory.buffer);
		    	uheapu8 = new Uint8Array(umemory.buffer);
				if (umemory.buffer instanceof SharedArrayBuffer) {
					_uvm_spacep = _kernexp.new_uvmspace();
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
			let orgkesp, didret = false;
			while (!didret) {
				try {
					orgkesp = __kesp.value;
					_kernexp.lwp_trampoline();
					didret = true;
				} catch (err) {
					if (err !== unwind4exec) {
						//debugger;
						//self.postMessage({cmd: "lwp_died"});
						//self.close();
						throw err;
					} else {
						didret = false;
						console.log("reset kernel stack-pointer old: %d new %d", __kesp.value, orgkesp);
						__kesp.value = orgkesp;
						continue;
					}
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
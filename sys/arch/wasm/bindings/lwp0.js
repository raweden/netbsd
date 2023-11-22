
const memory_min = 2000;
const memory_max = 2000;
const wasm_pgsz = (1 << 16);
let init_worker;
let opfs_ext4_worker;
let rblkdev_init = false;
let __kmodule;
let _exports;
let kmemory;
let kmembuf;
let kmemdata;
let kheap32;
let kheapu8;
let __sysent;
let __curlwp;

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
var UTF8Decoder = typeof TextDecoder !== 'undefined' ? new TextDecoder('utf8') : undefined;

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

// Returns the number of bytes the given Javascript string takes if encoded as a UTF8 byte array, EXCLUDING the null terminator byte.
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

function setupInitData(buffer) {

	let u8 = new Uint8Array(buffer);
	let data = new DataView(buffer);

	if (data.getUint8(0) == 0x00 || data.getUint8(1) == 0x61 || data.getUint8(2) == 0x73 || data.getUint8(3) == 0x6D) {
		// reading WebAssembly module.
		return setupInitWasmData();
	} else {

	}

	let off = 0;
	let end = buffer.byteLength - 1;
	let cnt = 0;
	while (off < end) {
		let dst = data.getUint32(off, true);
		let bufsz = data.getUint32(off + 4, true);
		off += 8;
		u8_memcpy(u8, off, bufsz, kheapu8, dst);
		off += bufsz;
		cnt++;
	}

	console.log("applied initial Memory from %d segments", cnt);
}

function setupInitWasmData(buffer) {

	let u8 = new Uint8Array(buffer);
	let data = new DataView(buffer);
	let off = 0;
	let len = u8.byteLength;

	if (!(data.getUint8(0) == 0x00 || data.getUint8(1) == 0x61 || data.getUint8(2) == 0x73 || data.getUint8(3) == 0x6D)) {
		return;
	}

	off += 4;

	function readULEB128(as64) {
        // consumes an unsigned LEB128 integer starting at `off`.
        // changes `off` to immediately after the integer
        as64 = (as64 === true);
        let tmp = off;
        let result = BigInt(0);
        let shift = BigInt(0);
        let byte = 0;
        do {
            byte = u8[tmp++];
            result += BigInt(byte & 0x7F) << shift;
            shift += 7n;
        } while (byte & 0x80);

        if (!as64 && result < 4294967295n)
            result = Number(result);

        off = tmp;

        return result;
    }

    let cnt = 0;
    let memmin = 0x7FFFFFFF
    let memmax = 0;

    while (off < len) {
        let start = off;
        let type = data.getUint8(off++);
        let size = readULEB128();

        if (type == 0x0B) {
        	let tmp = off;
        	let cnt = readULEB128();
        	for (let i = 0; i < cnt; i++) {
		        let kind = readULEB128();
		        let initOffset = 0;
		        if (kind == 0x00) {
		        	if (data.getUint8(off) == 0x41) {
		        		off++;
		        		initOffset = readULEB128();
		        		if (data.getUint8(off) == 0x0B) {
		        			off++;
		        		} else {
		        			throw TypeError("Unexpected instruction");
		        		}
		        	} else {
		        		throw TypeError("Unexpected instruction");
		        	}

		            let datasz = readULEB128();
		            u8_memcpy(u8, off, datasz, kheapu8, initOffset);

		            if (initOffset < memmin) {
		            	memmin = initOffset;
		            }

		            if ((initOffset + datasz) > memmax) {
		            	memmax = (initOffset + datasz);
		            }

		            off += datasz;
		        } else if (kind == 0x01) {
		            console.warn("data segment of type `init b*, mode passive` is not implemented");
		            break;
		        } else if (kind == 0x02) {
		            console.warn("data segment of type `init b*, mode active {memory, offset }` is not implemented");
		            let memidx = readULEB128();
		            break;
		        } else {
		            console.warn("undefined data-segment mode!");
		            break;
		        }
        	}

        	console.log("applied initial Memory from %d segments", cnt);
        	console.log("memory-min %d memory-max: %d", memmin, memmax);

        	off = tmp + size;
        } else {
        	console.log("skip %s", type.toString(16));
        	off += size;
        }
    }

    return {min: memmin, max: memmax};
}

// void kern.cons_write(i32, i32, i32, i32)
function kcons_write(buf, bufsz, flags, level) {
	let str = UTF8ArrayToString(kheapu8, buf, bufsz);
	if (str.startsWith("panic:")) {
		console.error(str);
	} else {
		str.trimEnd();
		console.log("%c%s", "font-family: monospace;", str);
	}
	
}

// void kern.__copyout(i32, i32, i32, i32)
function kern_console_log(buf, bufsz, flags, level) {
	let str = UTF8ArrayToString(kheapu8, buf, bufsz);
	if (str.startsWith("panic:")) {
		console.error(str);
	} else {
		str.trimEnd();
		console.warn("%c%s", "font-family: monospace;", str);
	}
	
}

// i32 kern.__copyout(i32, i32, i32)
function kcopyout(kaddr, uaddr, len) {
	return -1;
}

// i32 kern.__copyoutstr(i32, i32, i32, i32)
function kcopyoutstr(uaddr, kaddr, len, lenptr) {
	return -1;
}

// i32 kern.__copyin(i32, i32, i32)
function kcopyin(uaddr, kaddr, len) {
	return -1;
}

// i32 kern.__copyinstr(i32, i32, i32, i32)
function kcopyinstr(uaddr, kaddr, len, lenptr) {
	return -1;
}

// i32 kern.__fetch_user_data(i32, i32, i32)
function kfetch_user_data(uaddr, kaddr, len) {
	return -1;
}

// i32 kern.__store_user_data(i32, i32, i32)
function kstore_user_data(uaddr, kaddr, len) {
	return -1;
}

// i32 kern.ucas32(i32, i32, i32, i32)
function kern_ucas32(uaddr, oval, nval, ret)
{
	return -1;
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
function kcsr_sstatus_set(ptr) {
	// from user space
	return;
}

// [i32] → []
function ksbi_extcall(tf) {
	// from user space
	return;
}


// i32 kern.fueword(i32, i32)
function kern_fueword(ptr, vptr) {
	return -1;
}
// https://manpages.debian.org/bullseye/freebsd-manpages/fubyte.9freebsd.en.html

// i32 kern.fueword32(i32, i64)
function kern_fueword32(ptr, vptr) {
	return -1;
}

// i32 kern.fueword32(i32, i32)
function kern_fueword64(ptr, vptr) {
	return -1;
}

// i32 kern.subyte(i32, i32)
function kern_subyte(ptr, byte) {
	// to userspace.
	return -1;
}

// i32 kern.suword(i32, i32)
function kern_suword(ptr, word) {
	return -1;
}

// i32 kern.suword32(i32, i32)
function kern_suword32(ptr, word) {
	return -1;
}

// i32 kern.suword32(i32, i64)
function kern_suword64(ptr, word) {
	return -1;
}

// i32 kern.casueword(i32, i32, i32, i32)
function kern_casueword(ptr, oldval, oldvalp, newval) {
	return -1;
}

// i32 kern.casueword32(i32, i32, i32, i32)
function kern_casueword32(ptr, oldval, oldvalp, newval) {
	return -1;
}

// void kern.panic_abort(void)
function kern_panic() {
	throw new Error("kernel_panic");
}

// void kern.random_source(i32, i32)
function kern_random_source(buf, bufsz) {
	let tmp = new Uint8Array(bufsz);
    crypto.getRandomValues(tmp);
    u8_memcpy(tmp, 0, bufsz, kheapu8, buf);
    return bufsz;
}

// i32 emscripten.memcpy_big(i32, i32, i32)
function emscripten_memcpy_big(dst, src, len) {
	kheapu8.copyWithin(dst, src, src + len);
}

function kthread_dispatch_sync() {
	throw new Error("should never be called in main");
}

let td_workers = {};
let workers = [];
let pending_td = {};

function wasm_thread_alloc(td) {
	console.log("wasm_thread_alloc td = %d", td);
	if (td == 0) {
		throw TypeError("tried to alloc thread with NULL");
	}
	let kstack = kmemdata.getUint32(td + 844, true);

	pending_td[td] = {td: td, kstack: kstack};
}



function wasm_sched_fork(td, childtd) {
	console.log("wasm_sched_fork td = %d", td);
	if (td == 0 || childtd == 0) {
		throw TypeError("tried to sched thread with NULL"); // trd thd thr
	}
	let pending = pending_td[childtd];
	if (!pending) {
		console.warn("wasm_sched_add() called but no worker pending");
		return;
	}
}

function wasm_sched_throw(td) {
	console.log("wasm_sched_throw td = %d", td);
	throw new Error("Invalid state sched_throw should not be called on main kernel thread");
}



function addEventListenerToThread(worker, trdobj) {

	function onThreadMessage(evt) {
		let msg = evt.data;
		let cmd = msg.cmd;
		if (cmd == "wasm_thread_alloc") {

			let args = msg.args;
			
			wasm_thread_alloc.apply(null, args);

			let wqidx = msg.wqidx;
			if (wqidx !== 0) {
				Atomics.store(kheap32, wqidx, 123)
				Atomics.notify(kheap32, wqidx);
			}
		} else if (cmd == "wasm_sched_add") {

			let args = msg.args;
			let td = args[0];
			if ((msg.user_memory || msg.user_module) && pending_td.hasOwnProperty(td)) {
				let trd = pending_td[td];
				if (msg.user_memory) {
					trd.user_memory = msg.user_memory;
				}
				if (msg.user_module) {
					trd.user_module = msg.user_module;
				}

				if (msg.in_fork) {
					trd.fork_from = {
						id: msg.in_fork
					}

					if (msg.fork_frame) {
						trd.fork_from.args = msg.fork_frame;
					}
				}
			}

			wasm_sched_add.apply(null, args);
			
			let wqidx = msg.wqidx;
			if (wqidx !== 0) {
				Atomics.store(kheap32, wqidx, 123)
				Atomics.notify(kheap32, wqidx);
			}
			
		} else if (cmd == "wasm_sched_fork") {
			
			let args = msg.args;
			
			wasm_sched_fork.apply(null, args);

			let childtd = args[1];
			
			if (Number.isInteger(childtd) && Number.isInteger(msg.ustack) && pending_td.hasOwnProperty(childtd)) {
				let thread = pending_td[childtd];
				thread.ustack = msg.ustack;
			}
			
			let wqidx = msg.wqidx;
			if (wqidx !== 0) {
				Atomics.store(kheap32, wqidx, 123)
				Atomics.notify(kheap32, wqidx);
			}
		} else if (cmd == "wasm_sched_throw") {
			
			let args = msg.args;
			console.log("sched_throw called for childtd = %d", trdobj.td);
			// TODO: remove td_workers[td]

		} else if (cmd == "") {

		} else if (cmd == "") {

		} else if (cmd == "") {

		}
	}

	function onThreadError(evt) {
		console.error(evt);
	}

	function onThreadMessageError(evt) {
		console.error(evt);
	}

	worker.addEventListener("message", onThreadMessage);
	worker.addEventListener("error", onThreadError);
	worker.addEventListener("messageerror", onThreadMessageError);


	trdobj.removeEventListeners = function() {
		worker.removeEventListener("message", onThreadMessage);
		worker.removeEventListener("error", onThreadError);
		worker.removeEventListener("messageerror", onThreadMessageError);
	}
}

function klwp_spawn(l1, l2, stackptr, stacksz) {
	console.log("klwp_spawn l1 = %d l2 = %d stackptr = %d stacksz = %d", l1, l2, stackptr, stacksz);

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
	self.postMessage(msg);
}

function wasm_sched_add(td, flags) {
	console.log("wasm_sched_add td = %d", td);
	if (td == 0) {
		throw TypeError("tried to sched thread with NULL");
	}
	let pending = pending_td[td];
	if (!pending) {
		console.warn("wasm_sched_add() called but no worker pending");
		return;
	}

	//let ps_arg0 = UTF8ArrayToString(kheapu8, td + 472);
	// as reading thread->td_comm at this stage always results the name of the parent thread name.. 
	// giving the thread the name of the origin address is far better.
	let debugName = "0x" + ((td).toString(16).padStart(8, 0)) + "\x20(uthread)";
	let worker;
	

	// setup proc->p_sysent to 151452 here, to allow systemcalls.
	let proc = kmemdata.getUint32(td + 4, true);
	if (proc == 0) {
		throw TypeError("td->td_proc should not be NULL");
	}
	let p_sysent = kmemdata.getUint32(proc + 672, true);
	console.log("proc->p_sysent = %d (old) %d (new)", p_sysent, __sysent)
	kmemdata.setUint32(proc + 672, __sysent, true);
	// init: 17674240 proc0: 223256
	
	// TODO: we should move the Worker creation to wasm_sched_alloc
	worker = new Worker("uthread.js", {name: debugName});
	if (!init_worker) {
		init_worker = worker;
	}
	let msg = {
		cmd: "spawn_thread",
		kernel_module: __kmodule,
		kernel_memory: kmemory,
		__curlwp: pending.td,
		__stack_pointer: pending.kstack,
	};

	if (pending.user_module) {
		msg.user_module = pending.user_module;
		delete pending.user_module
	}
	
	if (pending.user_memory) {
		msg.user_memory = pending.user_memory;
		delete pending.user_memory;
	}

	if (pending.fork_from) {
		msg.fork_from = pending.fork_from;
		delete pending.fork_from;
	}

	if (Number.isInteger(pending.ustack)) {
		msg.ustack = pending.ustack;
		delete pending.ustack;
	}

	addEventListenerToThread(worker, pending);
	
	worker.postMessage(msg);

	if (!worker) {
		console.warn("wasm_sched_add() called more than once for two different threads..");
		return;
	}

	td_workers[td] = pending;
	delete pending_td[td];
	pending.worker = worker;
	workers.push(worker);
}

function wasm_clock_gettime(ts) {

	let now = performance.timeOrigin + performance.now();

	kmem.setInt32(ts, (now / 1000) | 0, true);						// seconds
	kmem.setInt32(ts + 4, ((now % 1000) * 1000 * 1000) | 0, true);	// nanoseconds

	return 0;
}

// mutates the min & max memory on binaries that supports it.
function configureWasmBinary(buffer) {
	let data = new DataView(buffer);
	let magic = data.getUint32(0, true);
    let version = data.getUint32(4, true);
    let off = 0;
	let len = u8.byteLength;

	if (data.getUint32(0, true) != 0x6d736100) {	// \0asm
		throw new TypeError("Invalid signature");
	}

	off += 8;

	function readULEB128(as64) {
        // consumes an unsigned LEB128 integer starting at `off`.
        // changes `off` to immediately after the integer
        as64 = (as64 === true);
        let tmp = off;
        let result = BigInt(0);
        let shift = BigInt(0);
        let byte = 0;
        do {
            byte = u8[tmp++];
            result += BigInt(byte & 0x7F) << shift;
            shift += 7n;
        } while (byte & 0x80);

        if (!as64 && result < 4294967295n)
            result = Number(result);

        off = tmp;

        return result;
    }

    let cnt = 0;
    let memmin = 0x7FFFFFFF
    let memmax = 0;

    while (off < len) {
        let start = off;
        let type = data.getUint8(off++);
        let size = readULEB128();

        switch (type) {
        	case 0x02:	// import
        	{

        	}
        	case 0x05:	// memory
        	{

        	}
        	case 0x00: 	// custom
        	case 0x01:	// type
        	case 0x03:	// func
        	case 0x04:	// table
        	case 0x06:	// global
        	case 0x07:	// export
        	case 0x08:	// start
        	case 0x09:	// element
        	case 0x0a:	// code
        	case 0x0b:	// data
        	case 0x0c:	// data-count
        		off += size;
        		break;
        	default: 
        		throw new Error("Invalid section code");

        }
    }
}

function onKernBootstrapFail(err) {

}

// Somehow the busy state of the lwp0 prevents message to be delivered to the worker, like it not beeing sent.
// Previously the file-system worker was spawned before the bootstrap itself, but now once the posting of the message
// is done at will of wasm it does not get delivered until the wasm execution is killed. To walk-around the issue the
// block device worker is spawned on the main thread instead.
function kspawn_blkdev_worker(rblkdev, init_cmd) {
	if (rblkdev_init) {
		console.error("logics for handling multiple block-device workers not implemented..");
		throw new Error("block device spawned twice");
	}
	rblkdev_init = true;
	self.postMessage({
		cmd: "rblkdev_init_signal",
		kmemory: kmemory,
		rblkdev_head: rblkdev,
		rblkdev_init: init_cmd,
	});
}

function kexec_ioctl(cmd, argp) {
	switch (cmd) {

		case 512: {


			break;
		}

		case 513: {


			break;
		}
	}

	return -1;
}


function bootstrap_kern(kmodule) {
	console.timeEnd("wasm compile");
	__kmodule = kmodule;
	kmemory = new WebAssembly.Memory({
		minimum: memory_min, // 188
		maximum: memory_max,
		shared: true,
	});

	let rsvdmem = []; // resvmem
	let IOM_BEGIN;
	let IOM_END;
	let netbsd_wakern_info;
	let iosurfaceIPCHead;
	let arr = WebAssembly.Module.customSections(kmodule, "com.netbsd.kernel-locore");
	if (arr.length == 1) {
		let text, json, buffer = arr[0];
		console.log("found com.netbsd.kernel-locore section");
		console.log(buffer);
		text = UTF8ArrayToString(new Uint8Array(buffer), 0, buffer.byteLength);
		json = JSON.parse(text);
		console.log(json);
		netbsd_wakern_info = json;
	}

	// TODO: implement something similar to this to with ease set env for bootstrap.
	let __kenv = {
		'RUMP_THREADS': 256,
		'RUMP_VERBOSE': true,
		'WASM_MEMLIMIT': 1000448,
		//RUMP_NVNODES
		//RUMP_MODULEBASE
		//RUMP_BLKFAIL
		//RUMP_BLKSECTSHIFT
		//RUMP_BLKFAIL_SEED
		//RUMP_SHMIF_CAPENABLE
		//RUMPUSER_PARAM_NCPU
		//RUMPUSER_PARAM_HOSTNAME
	};

	function l1map(ptr) {
		//let s5 = (VM_KERNEL_SIZE >> SEGSHIFT); 			// # of megapages
		//let s6 = (NBSEG >> (PGSHIFT - PTE_PPN_SHIFT))	// load for ease
		//let s7 = PTE_KERN | PTE_HARDWIRED | PTE_R | PTE_W | PTE_X;
	}

	// invoked after memory has been setup
	function __netbsd_locore() {
		const info = netbsd_wakern_info;
		const PAGE_SIZE = 4096;

		let initmem_lo = kmemdata.getUint32(info.__start__init_memory, true);
		let initmem_hi = kmemdata.getUint32(info.__stop__init_memory, true);
		let iomem_start;
		let iomem_end;

		if (info.__wasm_meminfo) {
			let ptr = info.__wasm_meminfo;
			let bootspace_p = kmemdata.getUint32(ptr, true);
			let physmem_p = kmemdata.getUint32(ptr + 4, true);
			iomem_start = kmemdata.getUint32(ptr + 8, true);
			iomem_end = kmemdata.getUint32(ptr + 12, true);
			IOM_BEGIN = iomem_start;
			IOM_END = iomem_end;

			if (physmem_p) {
				kmemdata.setUint32(physmem_p, memory_max * wasm_pgsz, true);
			}
		}

		if (info.__builtin_iosurfaceAddr) {
			iosurfaceIPCHead = info.__builtin_iosurfaceAddr;
		}

		console.log("availble memory before iomem_start: %d", iomem_start - initmem_hi);

		// we want nothing in the first page..
		rsvdmem.push({addr: 0, size: PAGE_SIZE});


		rsvdmem.push({addr: initmem_lo, size: initmem_hi - initmem_lo});
		//rsvdmem.push({addr: iomem_start, size: iomem_end - iomem_start});

		// whats allocated here needs to fit before iomem_start or be placed after iomem_end
		let mem_l2tbl = {size: PAGE_SIZE, align: PAGE_SIZE};
		let mem_l1tbl = {size: Math.ceil((kmemdata.byteLength / PAGE_SIZE) / 1024) * PAGE_SIZE, align: PAGE_SIZE};
		let mem_lwp0_stackp = {size: Math.ceil(info.hint_min_stacksz / wasm_pgsz) * wasm_pgsz, align: PAGE_SIZE};
		let sysrsvd = [mem_l2tbl, mem_l1tbl, mem_lwp0_stackp];
		let boot_pde, l1_addr;

		let memptr = initmem_hi;

		function alignUp(bytes) {
			if (bytes == 0)
				return memptr;

			let rem = memptr % bytes;
			if (rem !== 0) {
				rem = bytes - rem;
				memptr += rem;
			}

			return memptr;
		}

		console.log(sysrsvd);
		// doing this rem computation is slower than the bitshift approach, but we only do this memory setup once..
		
	
		// 4096 bytes for L2 pde table for boot process.
		let size, ptr = alignUp(PAGE_SIZE);
		size = PAGE_SIZE;
		if (memptr < iomem_end && memptr + size >= iomem_start) {
			memptr = iomem_end;
		}
		boot_pde = ptr;
		rsvdmem.push({addr: ptr, size: size});
		memptr += size;

		// pages for L1 pte tables (for now mapping the entire kernel memory)
		let pte_cnt = Math.ceil((kmemdata.byteLength / 4096) / 1024);
		size = PAGE_SIZE * pte_cnt;
		if (memptr < iomem_end && memptr + size >= iomem_start) {
			memptr = iomem_end;
		}
		ptr = alignUp(PAGE_SIZE);
		l1_addr = ptr;
		rsvdmem.push({addr: ptr, size: size});
		memptr += size;
		
		// hint_min_stacksz rounded to whole WebAssembly page-size
		size = Math.ceil(info.hint_min_stacksz / wasm_pgsz) * wasm_pgsz;
		if (memptr < iomem_end && memptr + size >= iomem_start) {
			memptr = iomem_end;
		}
		ptr = alignUp(PAGE_SIZE);
		let lwp0_stackp = size + ptr;
		rsvdmem.push({addr: ptr, size: size});
		memptr = lwp0_stackp;
		info.lwp0_stackp = lwp0_stackp;


		kmemdata.setUint32(info.__stop__init_memory, initmem_hi, true);
		kmemdata.setUint32(info.bootstrap_pde, boot_pde, true);
		kmemdata.setUint32(info.l1_pte, l1_addr, true);
		//kmemdata.setUint32(info.fdt_base, fdt_base, true);
		kmemdata.setUint32(info.__start_kern, 0, true);
		kmemdata.setUint32(info.__stop_kern, memptr, true);
		info.pte_cnt = pte_cnt;
		info.boot_pde = boot_pde;

		console.log("after init-locore");
		console.log("first_avail: %d", memptr);
		console.log("IOM_BEGIN: %d", IOM_BEGIN);
		console.log("IOM_END: %d", IOM_END);

		if (info.__physmemlimit) {
			kmemdata.setUint32(info.__physmemlimit, memory_max * wasm_pgsz, true);
		}

		if (info.avail_end) {
			//kmemdata.setUint32(info.avail_end, kmemdata.byteLength, true);
		}

		if (info.__first_avail) {
			kmemdata.setUint32(info.__first_avail, memptr, true);
		}

		if (info.lwp0uarea) {
			kmemdata.setUint32(info.lwp0uarea, lwp0_stackp, true);
		}

		if (info.l2_addr) {
			kmemdata.setUint32(info.l2_addr, boot_pde, true);
		}

		rsvdmem.sort(function(a, b) {
			if (a.addr > b.addr) {
				return 1;
			} else if (a.addr < b.addr) {
				return -1;
			}

			return 0;
		});

		console.log(rsvdmem);

		if (info.bootinfo) {
			__netbsd_bootinfo(info.bootinfo);
		}

		let pte = l1_addr;
		//let pgcnt = 
		//l1_addr.setUint32(pte, );
	}

	function __netbsd_bootinfo(addr) {
		let nent_ptr = addr;
		let ptr = addr + 4;
		let cnt = 0;

		const BTINFO_BOOTPATH = 0;
		const BTINFO_ROOTDEVICE = 1;
		const BTINFO_BOOTDISK = 3;
		const BTINFO_NETIF = 4;
		const BTINFO_CONSOLE = 6;
		const BTINFO_BIOSGEOM = 7;
		const BTINFO_SYMTAB = 8;
		const BTINFO_MEMMAP = 9;
		const BTINFO_BOOTWEDGE = 10;
		const BTINFO_MODULELIST = 11;
		const BTINFO_FRAMEBUFFER = 12;
		const BTINFO_USERCONFCOMMANDS = 13;
		const BTINFO_EFI = 14;
		const BTINFO_EFIMEMMAP = 15;
		const BTINFO_PREKERN = 16;

		if (kmemdata.getInt32(nent_ptr, true) != 0) {
			console.warn("bootinfo not uninitalized");
			return;
		}

		// int len
		// int type
		// char bootpath[80]
		// (size 88)
		function btinfo_bootpath(path) {
			let strlen = lengthBytesUTF8(path);
			if (strlen >= 80) {
				throw new Error("ENAMETOLONG");
			}
			kmemdata.setInt32(ptr, 88, true);
			kmemdata.setInt32(ptr + 4, BTINFO_BOOTPATH, true);
			stringToUTF8Bytes(kheapu8, ptr + 8, path);
			ptr += 88;
			cnt++;
		}

		// int len
		// int type
		// char devname[16]
		// (size 24)
		function btinfo_rootdevice(path) {
			let strlen = lengthBytesUTF8(path);
			if (strlen >= 16) {
				throw new Error("ENAMETOLONG");
			}
			kmemdata.setInt32(ptr, 24, true);
			kmemdata.setInt32(ptr + 4, BTINFO_ROOTDEVICE, true);
			stringToUTF8Bytes(kheapu8, ptr + 8, path);
			ptr += 24;
			cnt++;
		}

		//  0: int len
		//  4: int type
		//  8: int labelsector
		// 12: uint16_t type
		// 14: uint16_t checksum
		// 16: char packname[16]
		// -- 2 byte padding --
		// 32: int biosdev
		// 36: int partition
		// (size 40)
		function btinfo_bootdisk(labelsector, type, cksum, packname, biosdev, partition) {
			let strlen = lengthBytesUTF8(packname);
			if (strlen >= 16) {
				throw new Error("ENAMETOLONG");
			}
			kmemdata.setInt32(ptr, 40, true);
			kmemdata.setInt32(ptr + 4, BTINFO_BOOTDISK, true);
			kmemdata.setInt32(ptr + 8, labelsector, true);
			kmemdata.setInt16(ptr + 12, type, true);
			kmemdata.setInt16(ptr + 14, cksum, true);
			stringToUTF8Bytes(kheapu8, ptr + 16, packname);
			kmemdata.setInt32(ptr + 8, biosdev, true);
			kmemdata.setInt32(ptr + 8, partition, true);
			ptr += 40;
			cnt++;
		}

		//  0: int len
		//  4: int type
		//  8: int biosdev;
		// 12: int64_t startblk;
		// 20: uint64_t nblks;
		// 28: daddr_t matchblk;
		// 36: uint64_t matchnblks;
		// 44: uint8_t matchhash[16];	/* MD5 hash */
		// (size 60 [packed])
		function btinfo_bootwedge(path) {

		}

		const BIM_Memory = 1;	/* available RAM usable by OS */
		const BIM_Reserved = 2;	/* in use or reserved by the system */
		const BIM_ACPI = 3;		/* ACPI Reclaim memory */
		const BIM_NVS = 4;		/* ACPI NVS memory */
		const BIM_Unusable = 5;	/* errors have been detected */
		const BIM_Disabled = 6;	/* not enabled */
		const BIM_PMEM = 7;		/* Persistent memory */
		const BIM_PRAM = 12;	/* legacy NVDIMM (OEM defined) */

		function btinfo_memmap(entries) {
			
			kmemdata.setInt32(ptr, 12 + (20 * entries), true);
			kmemdata.setInt32(ptr + 4, BTINFO_MEMMAP, true);
			kmemdata.setInt32(ptr + 8, entries.length, true);

			ptr += 12;
			let len = entries.length;
			for (let i = 0; i < len; i++) {
				let ent = entries[i];
				if (ent.addr < 0 || ent.size < 0) {
					throw new RangeError("entry addr or size must not be less-than zero");
				}
				kmemdata.setBigUint64(ptr, BigInt(ent.addr), true);
				kmemdata.setBigUint64(ptr + 8, BigInt(ent.size), true);
				kmemdata.setUint32(ptr + 16, ent.type, true);
				ptr += 20;
			}

			cnt++;
		}

		{
			// merge reserved segments that starts where previous ends 
			// or if the free space separating them is less than a page.
			let last = rsvdmem[0];
			let lastend = last.addr + last.size;
			let res = [last];
			let len = rsvdmem.length;
			for (let i = 1; i < len; i++) {
				let mem = rsvdmem[i];
				if (lastend == mem.addr) {
					last.size += mem.size;
					lastend += mem.size;
				} else if ((mem.addr - lastend) < 4096) {
					let diff = mem.addr - lastend;
					lastend = mem.addr + mem.size;
					last.size = (lastend - last.addr);
				} else {
					last = mem;
					res.push(last);
					lastend = mem.addr + mem.size;
				}
			}

			console.log(res);

			let entries = [];
			lastend = 0;
			len = res.length;
			for (let i = 0; i < len; i++) {
				let mem = res[i];
				if (lastend != mem.addr) {
					entries.push({addr: lastend, size: mem.addr - lastend, type: BIM_Memory});
				}
				// do not map IOM_BEGIN
				if (mem.addr != IOM_BEGIN) {
					entries.push({addr: mem.addr, size: mem.size, type: BIM_Reserved});
					lastend = mem.addr + mem.size;
				} else {
					lastend = IOM_END;
				}
			}

			/*
			// this was not the way
			const FIRST_16mb = 16777216
			if (lastend < FIRST_16mb) {
				entries.push({addr: lastend, size: FIRST_16mb - lastend, type: BIM_Memory});
				lastend = FIRST_16mb;
			}
			// neither was this
			let msgbuf_sz = 131072;
			if (lastend < kmemdata.byteLength - msgbuf_sz) {
				entries.push({addr: lastend, size: kmemdata.byteLength - msgbuf_sz - lastend, type: BIM_Memory});
				lastend = kmemdata.byteLength - msgbuf_sz;
			}*/

			if (lastend < kmemdata.byteLength) {
				entries.push({addr: lastend, size: kmemdata.byteLength - lastend, type: BIM_Memory});
			}

			console.log(entries);

			btinfo_rootdevice("/sd0a")
			btinfo_memmap(entries);
		}

		/*
		let last, ent = {addr: 0, size: 4096, type: BIM_Reserved};
		let mem = [ent];
		last = ent.addr + ent.size;
		ent = {addr: last, size: initmem_start - last, type: BIM_Memory};
		mem.push(ent);

		ent = {addr: initmem_start, size: initmem_end - initmem_start, type: BIM_Reserved};
		mem.push(ent);

		last = ent.addr + ent.size;
		ent = {addr: last, size: IOM_BEGIN - initmem_end, type: BIM_Memory};
		mem.push(ent);

		ent = {addr: IOM_BEGIN, size: IOM_END - IOM_BEGIN, type: BIM_Reserved};
		mem.push(ent);

		ent = {addr: IOM_END, size: kmemdata.byteLength - IOM_END, type: BIM_Reserved};
		mem.push(ent);
		*/
		

		

		//
		kmemdata.setInt32(nent_ptr, cnt, true);
	}
	
	// 
	function __netbsd_init_pgtbl_vm() {

		const PTE_P = 0x00000001;		/* Present */
		const PTE_W = 0x00000002;		/* Write */
		const PTE_U = 0x00000004;		/* User */
		const PTE_PWT = 0x00000008;		/* Write-Through */
		const PTE_PCD = 0x00000010;		/* Cache-Disable */
		const PTE_A = 0x00000020;		/* Accessed */
		const PTE_D = 0x00000040;		/* Dirty */
		const PTE_PAT = 0x00000080;		/* PAT on 4KB Pages */
		const PTE_PS = 0x00000080;		/* Large Page Size */
		const PTE_G = 0x00000100;		/* Global Translation */
		const PTE_AVL1 = 0x00000200;	/* Ignored by Hardware */
		const PTE_AVL2 = 0x00000400;	/* Ignored by Hardware */
		const PTE_AVL3 = 0x00000800;	/* Ignored by Hardware */
		const PTE_LGPAT = 0x00001000;	/* PAT on Large Pages */
		const PTE_NX = 0;		/* Dummy */

		const PGSHIFT = 12;
		const PAGE_SIZE = 4096;
		const PDE_SIZE = 4;
		let eax = 0;
		let ebx = kmemdata.getUint32(netbsd_wakern_info.l1_pte, true);
		let ecx = 0;

		function fillkpt() {
			if (ecx === 0)
				return;

			do {
				// from high to low
				kmemdata.setUint32(ebx, eax, true);
				ebx += PDE_SIZE;
				eax += PAGE_SIZE;
			} while (--ecx !== 0);
		}

		function fillkpt_blank() {
			if (ecx === 0)
				return;

			do {
				// from high to low
				kmemdata.setUint32(ebx, 0, true);
				ebx += PDE_SIZE;
			} while (--ecx !== 0);
		}

		// map page 0-131072 to physical
		ecx = 32;
		eax = 0;
		fillkpt();

		let start = kmemdata.getUint32(netbsd_wakern_info.__start__init_memory, true);
		let end = kmemdata.getUint32(netbsd_wakern_info.__stop__init_memory, true);
		ecx = Math.ceil((end - start) / PAGE_SIZE);
		eax = start;
		fillkpt();

		// IOMEM_START = 0x0a0000
		// IOMEM_END = 0x100000

	}

	function __netbsd_init_pgtbl() {

		const PTE_P = 0x00000001;

		const PGSHIFT = 12;
		const PAGE_SIZE = 4096;
		const PDE_SIZE = 4;
		let pde = kmemdata.getUint32(netbsd_wakern_info.l2_addr, true);
		let pte = kmemdata.getUint32(netbsd_wakern_info.l1_pte, true);
		let ptr1 = pte;
		let ptr2 = 0;
		let cnt = kmemdata.byteLength / 4096;

		// for now we simply map all memory so that virtual address = physical address
		for (let i = 0; i < cnt; i++) {
			// from high to low
			kmemdata.setUint32(ptr1, ptr2 | PTE_P, true);
			ptr1 += PDE_SIZE;
			ptr2 += PAGE_SIZE;
		}

		ptr1 = pde;
		ptr2 = pte;
		cnt = netbsd_wakern_info.pte_cnt

		for (let i = 0; i < cnt; i++) {
			// from high to low
			kmemdata.setUint32(ptr1, ptr2, true);
			ptr1 += PDE_SIZE;
			ptr2 += PAGE_SIZE;
		}

	}

	function preBootstrapCheck() {
		let addresses = netbsd_wakern_info.addresses;
		let len = addresses.length;
		for (let i = 0; i < len; i++) {
			let addr = addresses[i];
			let name = addr.name;
			let value = kmemdata.getUint32(addr.addr, true);
			console.log("%s = %s", name, value.toString(16).padStart('0', 8));
		}
	}

	const OPFS_EXT4_HEAD_ADDR = 0;


	if (OPFS_EXT4_HEAD_ADDR !== 0) {
		// spawning the process which holds the driver for the primary file-system
		opfs_ext4_worker = new Worker("/tinybsd/opfs+ext4/opfs+ext4-worker.js", {name: "opfs+ext4-driver"});
		opfs_ext4_worker.postMessage({
			cmd: "idbfs_dev_init_signal",
			kmemory: kmemory,
			idbfs_devmem: OPFS_EXT4_HEAD_ADDR,
			idbfs_devmemsz: 280,
		});
	}

	function __wasm_imgact_compile() {
		throw new Error("do not use this in main kernel thread");
	}

	function __wasm_imgact_instantiate() {
		throw new Error("do not use this in main kernel thread");
	}

	function __wasm_imgact_run() {
		throw new Error("do not use this in main kernel thread");
	}

	function __wasm_imgact_getstackptr() {
		throw new Error("do not use this in main kernel thread");
	}

	function __wasm_imgact_setstackptr(value) {
		throw new Error("do not use this in main kernel thread");
	}

	function __wasm_imgact_get_heap_base() {
		throw new Error("do not use this in main kernel thread");
	}

	function __wasm_imgact_set_heap_base(value) {
		throw new Error("do not use this in main kernel thread");
	}

	function kexecbuf_alloc(size) {
		return 8; // ENOEXEC
	}

	function kexecbuf_copy(kaddr, uaddr, size) {
		return 8; // ENOEXEC
	}

	function kexec_finish() {
		console.error("kexec_finish called on lwp0");
	}

	function exec_entrypoint() {
		throw new Error("do not use this in main kernel thread");
	}

	function __lock_debug(lockp, action) {
		/*let action_name = ["INIT", "LOCK", "UNLOCK", "FAILED"];
		if (action !== 3) {
			console.log("cpulock %d action %s lwp %d", lockp, action_name[action], __curlwp);
		} else {
			console.error("cpulock %d action %s lwp %d", lockp, action_name[action], __curlwp);
		}*/
	}

	function __kexec_ualloca(size) {

	}

	kmembuf = kmemory.buffer;
	kheapu8 = new Uint8Array(kmemory.buffer);
	kheap32 = new Int32Array(kmemory.buffer)
	kmemdata = new DataView(kmemory.buffer);

	fetch("./init.mem.wasm").then(function(req) {
		req.arrayBuffer().then(function(data) {

			//setupInitData(data); // in-house internal format.
			let dataparams = setupInitWasmData(data);

			__netbsd_locore();
			__netbsd_init_pgtbl();

			let __lwp0 = netbsd_wakern_info.lwp0;
			let __lwp0_stackp = netbsd_wakern_info.lwp0_stackp;
			__curlwp = __lwp0;

			if (!Number.isInteger(__lwp0) || __lwp0 === 0) {
				throw new RangeError("__lwp0 must be setup");
			}

			if (!Number.isInteger(__lwp0_stackp) || __lwp0_stackp === 0) {
				throw new RangeError("__lwp0_stackp must be setup");
			}

			preBootstrapCheck();	

			let importObject = {
				env: {
					memory: kmemory,
				},
				kern: {
					__curlwp: new WebAssembly.Global({value: 'i32', mutable: false}, __lwp0),
					__stack_pointer: new WebAssembly.Global({value: 'i32', mutable: true}, __lwp0_stackp),
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
					__lock_debug: __lock_debug,
					kexec_ualloca: __kexec_ualloca,				// khost_ioctl()
					// kern_exec
					execbuf_alloc: kexecbuf_alloc,
					execbuf_copy: kexecbuf_copy,
					exec_finish: kexec_finish,
					exec_entrypoint: exec_entrypoint,
					exec_ioctl: kexec_ioctl,
				},
				wasm_imgact: {
					compile: __wasm_imgact_compile,
					instantiate: __wasm_imgact_instantiate,
					call_main: __wasm_imgact_run,
					getstackptr: __wasm_imgact_getstackptr,
					setstackptr: __wasm_imgact_setstackptr,
					get_heap_base: __wasm_imgact_get_heap_base,
					set_heap_base: __wasm_imgact_set_heap_base
				},
				emscripten: {
					memcpy_big: emscripten_memcpy_big,
				}
			};
			// mod = new WebAssembly.Module(bytes); // <-- this is what actually enables us to impl. uselib() didn't know about this earlier.
			let instance = new WebAssembly.Instance(kmodule, importObject);
			_exports = instance.exports;

			console.log("spawned WebAssembly kernel in kthread.js");

		  	console.log(kmemory);

		  	_exports.global_start();
		  	console.log("global_start did return");
		});
	});
}

/*console.time("wasm compile");
fetch("./netbsd-kern.wasm").then(function(httpResponse) {

	httpResponse.arrayBuffer().then(function(bufferSource) {
		preConfigureWasmBinary(bufferSource);
		WebAssembly.compile(bufferSource).then(bootstrap_kern, onKernBootstrapFail);
	}, onKernBootstrapFail);
}, onKernBootstrapFail);*/

console.time("wasm compile");
WebAssembly.compileStreaming(fetch("./netbsd-kern.wasm")).then(bootstrap_kern, onKernBootstrapFail);

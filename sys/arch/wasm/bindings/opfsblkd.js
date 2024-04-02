// opfsblkd.js
// sys/arch/wasm/bindings/opfsblkd.js

// TODO:
// - to optimize allocation, use a layer of indirection for mapping blocks in the image file
//   this approach is similar to how virtual-memory is mapped, mapping lets say each 4mb of the file
//   since this kind of mapping is going to anyway render the image non compatable it might just be placed
//   at the beginning of the file as a header. Using such map would enable to pose to the kernel as if it was
//   1GB disk but it would only utilize the resources actully allocated.
//   - use specific signature for such chunk, and put a version i32 after the signature.
// - If w3c would come up with a attribute to disable double buffered write, that option would be suitable since
//   having these large file double buffers makes performance suffer.

/** @type {number} */
let aio_lwp;
/** @type {number} */
let aio_wchan;
/** @type {number} */
let aio_taskque;

/** @type {WebAssembly.Memory} */
let kmemory;
/** @type {SharedArrayBuffer} */
let kmembuf;
/** @type {Uint8Array} */
let kmem_u8;
/** @type {Int32Array} */
let kmem_32;
/** @type {DataView} */
let kmem;
/** @type {integer} */
let rblkdev_head = 0;
/** @type {FileSystemSyncAccessHandle} */
let fhandle;
/**
 * @type {Uint32Array}
 */
const buf4 = new Uint32Array();

const NO_REQ_SLOT = 32;

const READY_STATE_UNUSED = 0;
const READY_STATE_DONE = 3;

const OPFSBLK_STATE_UNUSED = 0;
const OPFSBLK_STATE_INIT = 1;
const OPFSBLK_STATE_READY = 2;
const OPFSBLK_STATE_KILL = 3;
const OPFSBLK_STATE_FAILURE_SETUP = 4;
const OPFSBLK_STATE_FAILURE = 5;

console.log("spawned opfsblkd.js thread");

// TODO: error-handling:
//		1. report error back and awaken the waiting thread!
//		2. there might be some error that can be recovered from.
// 		3. error that cannot be recovered from or would mean data-loss, abort op or crash thread.
//
// TODO: rewrite operation handling to either use throw/catch to report error or c-like et

function blkdevOnError(err) {
	console.error(err);
}

function blkdevRunLoop(wkret) {

	const seqaddr = (rblkdev_head + 44) / 4;
	const firstslot = (rblkdev_head + 48) / 4;
	let seqval, newseq, reqslot = firstslot;
	let reqs = [];
	let guard = 0;

	seqval = Atomics.load(kmem_32, seqaddr);

	while (true) {
		
		guard++;
		if (guard > 10000) {
			throw "RUNLOOP_STUCK?"
		}
		
		for (let i = 0; i < NO_REQ_SLOT; i++) {
			let old = Atomics.exchange(kmem_32, reqslot, 0);
			if (old !== 0) {
				reqs.push(old);
			}
			// TODO: read seq value from request, to ensure that we update seqval if less than, since we it can update while we read..
			reqslot++;
		}

		let len = reqs.length;

		if (len > 0) {
			//
			for (let i = 0; i < len; i++) {
				let ptr = reqs[i];
				let op = kmem.getInt32(ptr, true);
				let sync;
				let reqseq = kmem.getUint32(ptr, true);
				if (reqseq > seqval) {
					seqval = reqseq;
				}

				if (op === BIO_READ) {
					opfs_bio_read(op, ptr);
				} else if (op === BIO_WRITE || op === (BIO_WRITE|BIO_SYNC)) { // TODO: BIO_SYNC should be set as a async field.
					opfs_bio_write(op, ptr);
				} else {
					throw new Error("other operations not implemented..");
				}
			}
			//console.log(reqs);
			//exec_bio_ops(reqs);
		}

		newseq = Atomics.load(kmem_32, seqaddr);
		if (newseq != seqval) {
			console.log("could re-run to fetch more..");
		}

		let result = Atomics.waitAsync(kmem_32, seqaddr, seqval)
		//console.log(result);

		if (result.async) {
			result.value.then(blkdevRunLoop, blkdevOnError);
			return;
		} else {
			if (newseq == seqval) {
				newseq = Atomics.load(kmem_32, seqaddr);
			}
			//console.error("result.async is not true oldseq = %d newseq = %d", seqval, newseq);
			reqslot = firstslot;
			seqval = newseq;
		}
	}
}

/**
 * 
 * @param {number} task_ptr 
 * @returns {void}
 */
function aio_schedule(task_ptr) {
	const count = kmem.getUint32(aio_taskque, true); // never changes.
	let old, idx = (aio_taskque + 4) >>> 2;
	let found = false;
	for (let i = 0; i < count; i++) {
		old = Atomics.compareExchange(kmem_32, idx, 0, task_ptr);
		if (old === 0) {
			found = true;
			break;
		}
	}

	if (found) {
		// wake up the scheduler!
		let wchan = aio_wchan >> 2;
		Atomics.store(kmem_32, wchan, 1234);
		Atomics.notify(kmem_32, wchan, 1);
		return;
	}

	// schedular is busy.. try later
	setTimeout(aio_schedule, 0, task_ptr);
}

const BIO_READ = 0x01;
const BIO_WRITE = 0x02;
const BIO_SYNC = 0x04;
const BIO_INIT = 0x05;
const DEV_BSHIFT = 9;

let blkbufpool = [];
let blkbuf_1024;
let blkbuf_2048;
let blkbuf_4096;
let blkbuf_8192;

/**
 * @param {integer} sz A predefined buffer size (1024, 2048, 4096, 8192)
 * @returns {Uint8Array} 
 */
function blkbuf_get(sz) {
	switch (sz) {
		case 1024:
		{
			if (!blkbuf_1024) {
				blkbuf_1024 = new Uint8Array(1024);
			}
			return blkbuf_1024;
		}
		case 2048:
		{
			if (!blkbuf_2048) {
				blkbuf_2048 = new Uint8Array(2048);
			}
			return blkbuf_2048;
		}
		case 4096:
		{
			if (!blkbuf_4096) {
				blkbuf_4096 = new Uint8Array(4096);
			}
			return blkbuf_4096;
		}
		case 8192:
		{
			if (!blkbuf_8192) {
				blkbuf_8192 = new Uint8Array(8192);
			}
			return blkbuf_8192;
		}
	}

	throw new RangeError("NOT_BLKSZ_CACHED");
}

function opfs_bio_read(op, ptr) {
	let blksz = kmem.getUint32(ptr + 20, true);
	let blkno = Number(kmem.getBigInt64(ptr + 24, true));
	let daddr = kmem.getUint32(ptr + 16, true);
	let wchan = kmem.getUint32(ptr + 48, true);

	if (daddr === 0)
		throw new RangeError("cannot read/write from/to NULL");

	let buf = blkbuf_get(blksz);
	let off = blkno << DEV_BSHIFT;

	//console.log("ptr = %d op = %d blkno = %d (off %d) blksz = %d data-addr = %d", ptr, op, blkno, off, blksz, daddr);

	// TODO: check memory before copying

	fhandle.read(buf, {at: off});
	kmem_u8.set(buf, daddr);

	if (wchan === 0) {
		let waddr = (ptr + 4) >> 2;
		Atomics.store(kmem_32, waddr, READY_STATE_DONE);
		Atomics.notify(kmem_32, waddr);
	} else if (wchan == aio_wchan) {
		aio_schedule(ptr + 52);
	} else {
		console.error("req->wchan %d is not of supported value", wchan);
	}
}

function opfs_bio_write(op, ptr) {
	let blksz = kmem.getUint32(ptr + 20, true);
	let blkno = Number(kmem.getBigInt64(ptr + 24, true));
	let daddr = kmem.getUint32(ptr + 16, true);
	let wchan = kmem.getUint32(ptr + 48, true);

	if (daddr === 0)
		throw new RangeError("cannot read/write from/to NULL");

	// subarray should be OK since in this thread GC should not be a problem.
	let buf = kmem_u8.subarray(daddr, daddr + blksz);
	let off = blkno << DEV_BSHIFT;

	//console.log("ptr = %d op = %d blkno = %d (off %d) blksz = %d data-addr = %d", ptr, op, blkno, off, blksz, daddr);

	fhandle.write(buf, {at: off});

	if (wchan === 0) {
		let waddr = (ptr + 4) >> 2;
		Atomics.store(kmem_32, waddr, READY_STATE_DONE);
		Atomics.notify(kmem_32, waddr);
	} else if (wchan == aio_wchan) {
		aio_schedule(ptr + 52);
	} else {
		console.error("req->wchan is not of supported value", wchan);
	}
}

function opfs_bio_sync(ptr) {

} 

function exec_bio_ops(reqs) {

	let len = reqs.length;

	for (let i = 0; i < len; i++) {
		let ptr = reqs[i];
		let op = kmem.getInt32(ptr, true);
		let blksz = kmem.getUint32(ptr + 20, true);
		let blkno = Number(kmem.getBigInt64(ptr + 24, true));
		let daddr = kmem.getUint32(ptr + 16, true);

		if (daddr === 0)
			throw new RangeError("cannot read/write from/to NULL");

		if (op != BIO_READ)
			throw new Error("other operations not implemented..");

		let buf = blkbuf_get(blksz);
		let off = blkno << DEV_BSHIFT;

		//console.log("ptr = %d op = %d blkno = %d (off %d) blksz = %d data-addr = %d", ptr, op, blkno, off, blksz, daddr);


		fhandle.read(buf, {at: off});
		kmem_u8.set(buf, daddr);

		// blkno == 128 || blkno == 16
		// for now; lets just say that we did..
		let waddr = (ptr + 4) / 4;
		Atomics.store(kmem_32, waddr, READY_STATE_DONE);
		Atomics.notify(kmem_32, waddr);
	}
}

// handle.close()  		might return promise
// handle.flush()  		might return promise
// handle.getSize() 	might return promise
// handle.truncate(sz) 	might return promise
// handle.read(buffer, {at: 0})
// handle.write(buffer, {at: 0})

/**
 * 
 * @param {string} filepath A file-system path relative to the opfs root directory.
 * @param {boolean} finddir A boolean value that indicates the lookup is for a directory.
 * @returns {File}
 */
async function opfs_simple_namei(filepath, finddir) {

	let file, cnt, nodes, names, abs = false;
	if (finddir !== true)
		finddir = false;

	if (filepath.startsWith('/')) {
		abs = true;
		filepath = filepath.substring(1);
	}
	if (filepath.endsWith('/')) {
		finddir = true;
		filepath = filepath.substring(0, filepath.length - 1);
	}

	const root = await navigator.storage.getDirectory();
	const curdir = root;

	names = filepath.split('/');
	cnt = names.length;
	nodes = [root];

	for (const name of names) {
		let node;
		cnt--;
		if (name == '.') {
			continue;
		} else if (name == '..') {
			if (nodes.length < 2) {
				throw new Error("walked outside root");
			}
			nodes.pop();
			curdir = nodes[nodes.length - 1];
			continue;
		}

		if (cnt != 0 || finddir === true) {
			try {
				node = await curdir.getDirectoryHandle(name);
			} catch (err) {

			}
			if (node) {
				nodes.push(node);
				curdir = node;
				continue;
			}
		}

		node = await curdir.getFileHandle(name);

		if (node) {
			if (cnt != 0) {
				throw new TypeError("not dir");
			}
			file = node;
			continue;
		}
	}

	console.log(names);
	console.log(nodes);
	console.log(file);

	return file;
}

async function init_blkdev(rblkdev_head, init_cmd) {

	if (rblkdev_head === 0 || init_cmd === 0) {
		console.error("rblkdev_head and init_cmd must not be NULL");
		return;
	}

	let seqaddr = (rblkdev_head + 44) / 4;
	let seqval = Atomics.load(kmem_32, seqaddr);

	// remove init_cmd from request slot.
	let reqslot = (rblkdev_head + 48) / 4;
	for (let i = 0; i < 32; i++) {
		let old = Atomics.compareExchange(kmem_32, reqslot, init_cmd, 0);
		if (old === init_cmd)
			break;
		reqslot++;
	}

	let path_ptr, args_ptr;
	path_ptr = kmem.getUint32(init_cmd + 24, true);
	args_ptr = kmem.getUint32(init_cmd + 28, true);
	aio_lwp = kmem.getUint32(init_cmd + 32, true);
	aio_wchan = kmem.getUint32(init_cmd + 36, true);
	aio_taskque = kmem.getUint32(init_cmd + 40, true);

    let file, filepath = "netbsd-wasm-128.image";

    try {
    	file = await opfs_simple_namei(filepath);
    	fhandle = await file.createSyncAccessHandle();
    } catch (err) {
    	if (err.name == "NoModificationAllowedError") {
            console.warn("disk-file %s already opened", filepath);
        }
        console.error(err);
        throw err;
    }

	blkdevRunLoop(seqval);

	kmem.setInt32(rblkdev_head + 40, 1, true); // rblk_ftype

	let waddr = rblkdev_head >> 2;
	Atomics.store(kmem_32, waddr, OPFSBLK_STATE_READY);

	waddr = (init_cmd + 8) >> 2;
	Atomics.store(kmem_32, waddr, READY_STATE_DONE);
	Atomics.notify(kmem_32, waddr);

	waddr = rblkdev_head >> 2;
	Atomics.notify(kmem_32, waddr);
}


self.addEventListener("message", function (evt) {
	console.log(evt);
    let msg = evt.data;
    let cmd = msg.cmd;
    let sub = msg.subcmd;
    let arg = msg.arg;

    if (msg.cmd === "rblkdev_init_signal") {
        kmemory = msg.kmemory;
        if (!(kmemory instanceof WebAssembly.Memory))
            throw new TypeError("kmemory property must be of type WebAssembly.Memory");
        kmembuf = kmemory.buffer;
        kmem_32 = new Int32Array(kmembuf);
        kmem_u8 = new Uint8Array(kmembuf);
        kmem = new DataView(kmembuf);
        rblkdev_head = msg.rblkdev_head;
		lwp0_tasks_queue = msg.schedule_task;
        init_blkdev(msg.rblkdev_head, msg.rblkdev_init);
        return;
    } else if (msg.cmd === "rblkdev_kill_signal") {

        return;
    }

    throw new Error("other messages not handled");
});
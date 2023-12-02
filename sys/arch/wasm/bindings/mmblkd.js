//
// mmblkd.js
// sys/arch/wasm/bindings/mmblkd.js
// 
// 2023-11-23 18:45:48

// could we use the mmblkd to provide pseudo shared memory?
// - uses a rw_lock to ensure that only one thread can write at a time
// - on rw_exit syncronize/replicate memory to other instances.

let mem_map = {};
let umem_min_memidx = 1;
let umem_max_memidx = 4096;

let kmem_vmc;
let kmemory;
let kmem_32;
let kmem_u8;
let kmem_data;

let mmblkd_head;

let ports = [];

importScripts("jsonrpc.js");

let jsonrpc = new JSONRPC();

function onPortMessage(evt) {
    let data = evt.data;
    if (data.jsonrpc === JSONRPC_HEAD) {
        jsonrpc._handleMessage(evt);
        return;
    }
}

jsonrpc.addMethod("mmblkd.connect", function(target, params, transfer) {
    let port = params.port;
    port.start();

    port.addEventListener("message", onPortMessage);

    return true;
});

jsonrpc.addMethod("mmblkd.attach", function (target, params, transfer) {
    return mmblkd_attach(params.memory, params.vm_spacep);
});

jsonrpc.addMethod("mmblkd.detach", function (target, params, transfer) {
    return mmblkd_detach(params);
});

jsonrpc.addMethod("mmblkd.getMemory", function (target, params, transfer) {
    let memidx = params.memidx;

    if (mem_map.hasOwnProperty(memidx)) {
        throw new RangeError("INVALID_MEMIDX");
    }

    let uvmc = mem_map[memidx];

    let resp = {};
    resp.memidx = memidx;
    resp.memory = uvmc.memory;
    resp.vm_spacep = uvmc.vm_spacep;

    return resp;
});

function check_kmem(uvmc) {
    if (uvmc.memory.buffer.byteLength === uvmc.buffer.byteLength)
        return;
    
    let buf = uvmc.memory.buffer;
    kmem_32 = new Int32Array(buf);
    kmem_u8 = new Uint8Array(buf);
    kmem_data = new DataView(buf);
    uvmc.buffer = buf;
    uvmc.heap32 = kmem_32;
    uvmc.heapu8 = kmem_u8;
    uvmc.data = kmem_data;
}

function check_mem(uvmc) {
    if (uvmc.memory.buffer.byteLength === uvmc.buffer.byteLength)
        return;
    
    let buf = uvmc.memory.buffer;
    uvmc.buffer = buf;
    uvmc.heap32 = new Int32Array(buf);
    uvmc.heapu8 = new Uint8Array(buf);
    uvmc.data = new DataView(buf);
}

// vmc.memory === vmc.memory does not work, 
function find_memidx(memory) {
    for (let p in mem_map) {
        let vmc = mem_map[p];
        if (vmc.memory === memory || vmc.memory.buffer === memory.buffer) {
            return vmc.memidx;
        }
    }

    return -1;
}

function alloc_memidx(min, max) {
    if (!Number.isInteger(min))
        min = umem_min_memidx;
    if (!Number.isInteger(max))
        max = umem_max_memidx;


    for (let fd = min; fd <= max; fd++) {
        if (mem_map.hasOwnProperty(fd) === false) {
            return fd;
        }
    }

    return -1;
}

function init_blkdev(blkd_head, kmem, vm_spacep) {
    if (kmemory)
        return;
    
    mmblkd_head = blkd_head;
    kmemory = kmem;
    if (kmemory instanceof WebAssembly.Memory) {
        let kmembuf = kmemory.buffer;
        kmem_32 = new Int32Array(kmembuf);
        kmem_u8 = new Uint8Array(kmembuf);
        kmem_data = new DataView(kmembuf);
        kmem_vmc = {};
        kmem_vmc.buffer = kmembuf;
        kmem_vmc.memory = kmem;
        kmem_vmc.heap32 = kmem_32;
        kmem_vmc.heapu8 = kmem_u8;
        kmem_vmc.data = kmem_data;
        kmem_vmc.check_mem = check_kmem;
        kmem_vmc.vm_spacep = vm_spacep;
        let memidx = alloc_memidx(0, umem_max_memidx);
        if (memidx === -1)
            throw RangeError("memidx out of range");
        
        kmem_vmc.memidx = memidx;
        mem_map[memidx] = kmem_vmc;
    } else {
        return;
    }
}

function mmblkd_attach(memory, vm_spacep) {

    if (!(memory instanceof WebAssembly.Memory)) {
        console.error("Not instanceof WebAssembly.Memory");
        return;
    }
    
    memidx = find_memidx(memory);
    if (memidx !== -1) {
        console.error("memory instance already declared at %d", memidx);
        return;
    }

    if (vm_spacep === 0) {
        console.error("invalid uvm_space address %d", vm_spacep);
        return;
    }

    memidx = alloc_memidx(umem_min_memidx, umem_max_memidx);
    if (memidx === -1) {
        console.error("memidx out of range %d", memidx);
        return;
    }

    let buf = memory.buffer;
    let uvmc = {};
    uvmc.buffer = buf;
    uvmc.memory = memory;
    uvmc.heap32 = new Int32Array(buf);
    uvmc.heapu8 = new Uint8Array(buf);
    uvmc.data = new DataView(buf);
    uvmc.check_mem = check_mem;
    uvmc.vm_spacep = vm_spacep;
    uvmc.memidx = memidx;
    mem_map[memidx] = uvmc;
}

function mmblkd_detach(memidx) {
    
}

// copies between two memory containers.
function mmblkd_memcpy(src_memidx, src_addr, dst_memidx, dst_addr, length) {
    let dst, src;
    dst = mem_map[dst_memidx];
    src = mem_map[src_memidx];
    if (!src || !dst || src === dst) {
        console.error("invalid memidx");
        return;
    }

    if (!src || !dst) {
        console.error("invalid length");
        return;
    }

    dst.check_mem(dst);
    src.check_mem(src);

    if (length <= 0 || src_addr < 0 || dst_addr < 0 || src_addr + length > src.buffer.byteLength || dst_addr + length > dst.buffer.byteLength) {
        console.error("invalid range");
        return;
    }

    // anything less than 512 skip allocation of TypedArrayView(s) backstore is not allocated by object wrapper is..
    if (length <= 512) {
        let src = src.heapu8;
        let dst = dst.heapu8;
        let si = src_addr;
        let di = dst_addr;
        let se = si + length;

        while(si < se) {
            dst[di++] = src[si++];
        }

        return;
    }

    if ((src_addr == 0 || src_addr % 4 == 0) && (dst_addr == 0 || dst_addr % 4 == 0) && (length % 4 == 0)) {
        // everything is 32-bit aligned copy with i32 array
        let arr, start, end;
        start = src_addr !== 0 ? src_addr / 4 : 0;
        end = start + (length / 4);
        arr = src.heap32.subarray(start, end);
        start = dst_addr !== 0 ? dst_addr / 4 : 0;
        dst.heap32.set(arr, start);
    } else {
        // copy with u8 array.
        let arr, end;
        end = src_addr + length;
        arr = src.heapu8.subarray(src_addr, src_addr + length);
        dst.heapu8.set(arr, dst_addr);
    }
}

self.addEventListener("message", function (evt) {
    let msg = evt.data;
    let cmd = msg.cmd;
    let sub = msg.subcmd;
    let arg = msg.arg;

    if (msg.jsonrpc === JSONRPC_HEAD) {
        jsonrpc._handleMessage(evt);
        return;
    }

    if (cmd === "mmblkd_attach") {
        mmblkd_detach(msg.memory, msg.meminfo_ptr);
        return;
    } else if (cmd === "mmblkd_detach") {
        mmblkd_detach(msg.memidx);
        return;
    } else if (cmd === "mmblkd_memcpy") {
        mmblkd_memcpy(msg.src_memidx, msg.src_addr, msg.dst_memidx, msg.dst_addr, msg.length);
        return;
    } else if (cmd === "mmblkd_init_signal") {
        init_blkdev(msg.mmblkd_head, msg.kmemory, msg.meminfo_ptr);
        return;
    } else if (cmd === "mmblkd_kill_signal") {

        return;
    }

    console.log(evt);
    throw new Error("other messages not handled");
});

// The bootstrap.js files is responsible for the GUI binding between display-server.js and viewport within the browser
// 

//debugger;

/**
 * @typedef WorkerInfo
 * @type {object}
 * @property {integer} addr The address of the lwp in kernel space memory used to identify the thread from wasm land.
 * @property {string} name
 * @property {Worker} worker
 */

const DISPLAY_SERVER_URL = "display-server.js"

/** @type {WorkerInfo[]} */
const workers = [];
/** @type {Worker} */
const worker = new Worker("lwp0.js");
/** @type {Worker} */
let fb_worker;
let __dsrpc_server_head;
/** @type {Worker} */
let worker3;
let memstatDiv;
let kmem_debug_data;
let mmblkd_head;
/** @type {Worker} */
let mmblkd_worker;
/** @type {string} */
let canvasQuerySelector;

let windowManagerWorker;

let jsonrpc = new JSONRPC();
let dsrpc = new JSONRPC();
let dragDataTransferStack = [];
let dataTransferItemId = 1;
let dataTransferFileMap = new Map();
let dataTransferItemMap = new Map();

let workercnt = 0;

let commands = {
	lwp_spawn: function(msg) {

	},
	lwp_kill_sig: function(msg) {

	},
	rblkdev_init_signal: function(msg) {

	}
}

function lwp_error_handler(evt) {
    console.error("There is an error with your worker! %o", evt);
}

/**
 * @param {MessageEvent} evt 
 * @returns {boolean|void}
 */
function lwp0_message_handler(evt) {

	let msg = evt.data;
	let cmd = msg.cmd;
	if (cmd == "lwp_spawn") {
		let abi_wrapper = typeof msg.abi_path == "string" ? msg.abi_path : "lwp.js";
		let options = {};
		options.name = msg.name;
		let fmsg = Object.assign({}, msg);
		fmsg.cmd = "lwp_ctor";
		if (abi_wrapper && abi_wrapper == DISPLAY_SERVER_URL && windowManagerWorker) {
			windowManagerWorker.postMessage(fmsg);
			workers.push({addr: msg.__curlwp ? msg.__curlwp : null, name: msg.name, worker: windowManagerWorker});
			return;
		}

		let worker = new Worker(abi_wrapper, options);
        workers.push({addr: msg.__curlwp ? msg.__curlwp : null, name: msg.name, worker: worker});
		worker.addEventListener("message", lwp_message_handler);
        worker.addEventListener("error", lwp_error_handler);
		worker.postMessage(fmsg);
		workercnt++;
		if (abi_wrapper && abi_wrapper == DISPLAY_SERVER_URL) {
			windowManagerWorker = worker;
		}
		return;
	} else if (cmd == "rblkdev_init_signal") {
		let worker = new Worker("./opfsblkd.js");
		worker.postMessage(msg);
		workers.push({addr: null, name: "opfsblkd", worker: worker});
		return;
	} else if (cmd == "rblkdev_kill_signal") {

	} else if (cmd == "__kmem_debug_data") {
		setupMemoryDebugPanel(msg.mem, msg.ptr);
		return;
	} else if (cmd == "__dsrpc_server_head") {
		if (fb_worker) {
			fb_worker.postMessage(msg);
		} else {
			__dsrpc_server_head = msg.ptr;
		}
	}

	let ret = lwp_message_handler(evt);
	if (!ret)
		console.error("message not handled %o", evt);
}

/**
 * @param {MessageEvent} evt 
 * @returns {boolean|void}
 */
function lwp_message_handler(evt) {

	let msg = evt.data;
	let cmd = msg.cmd;
	if (cmd == "lwp_spawn") {
		console.error("lwp_spawn called from non lwp0 (enabled this later)");
		//return false;
		if (workercnt > 256) {
			console.error("more than 256 workers spawned, what is happening??");
			return false;
		}

		let options = {};
		options.name = msg.name;
		let worker = new Worker("lwp.js", options);
		let fmsg = Object.assign({}, msg);
		fmsg.cmd = "lwp_ctor";
        workers.push({addr: null, name: msg.name, worker: worker});
		worker.addEventListener("message", lwp_message_handler);
        worker.addEventListener("error", lwp_error_handler);
		worker.postMessage(fmsg);
		workercnt++;
		return true;
	} else if (cmd == "fs_ready") {
		windowManagerWorker.postMessage({cmd: "fs_ready"});
	} else if (cmd == "mmblkd_memcpy") {
		// TODO: used for testing, remove once no-longer needed.
		if (!mmblkd_worker) {
			console.error("mmblkd_worker does not spawn on mmblkd_memcpy");
			return;
		}

		mmblkd_worker.postMessage(msg);
		return true;

	} else if (cmd == "mmblkd_attach") {
		if (!mmblkd_worker) {
			setup_mmblkd_worker(msg);
		} else if (msg.mmblkd_head && msg.mmblkd_head != mmblkd_head) {
			console.error("msg.mmblkd_head is not the regular head");
			return;
		}

		mmblkd_worker.postMessage(msg);
		return true;

	} else if (cmd == "mmblkd_detach") {

		if (!mmblkd_worker) {
			console.error("mmblkd_worker does not spawn on mmblkd_detach");
			return;
		}

		mmblkd_worker.postMessage(msg);
		return true;
	} else if (cmd == "lwp_died") {

        let target = evt.target;
        let len = workers.length;
        for (let i = 0; i < len; i++) {
            if (target == workers[i].worker) {
                console.log("found self killed lwp %s", workers[i].name);
                workers.splice(i, 1);
                break;
            }
        }

		return true;
	} else if (cmd == "lwp_coredump") {

		let target = evt.target;
		let filename;
		if (typeof msg.filename == "string") {
			filename = msg.filename;
		} else {
			filename = "coredump-" + target.name + ".txt";
		}
		addCoredumpToList(msg.buffer, filename);

		return true;
	}

	return false;
}

worker.addEventListener("message", lwp0_message_handler);
worker.addEventListener("error", lwp_error_handler);

/** @type {HTMLUListElement} */
let _coredumpList;
let _coredumps = [];

function addCoredumpToList(buffer, filename) {

	if (!_coredumpList) {
		_coredumpList = document.createElement("ul");
		let parent = memstatDiv.parentElement;
		parent.insertBefore(_coredumpList, memstatDiv.nextElementSibling);
	}

	/** @type {HTMLLIElement} */
	let element = document.createElement("li");
	element.textContent = `save "${filename}" as file`;

	element.addEventListener("click", function coredumpOnClick(evt) {
		save_coredump(buffer, filename);
		element.removeEventListener("click", coredumpOnClick);
		element.parentElement.removeChild(element);
	});

	_coredumpList.appendChild(element);
}

/**
 * Opens a file save dialoge to save the provided ArrayBuffer as a file. Primarly used for debugging ATM.
 * @param {ArrayBuffer} buffer 
 * @param {String} filename Suggested filename
 */
async function save_coredump(buffer, filename) {
	let stream, handle;
	let opts = {};
	if (typeof filename == "string") {
		opts.suggestedName = filename;
	} else {
		opts.suggestedName = "coredump.txt";
	}

	try {
		handle = await window.showSaveFilePicker(opts);
		stream = await handle.createWritable({keepExistingData: false});

		await stream.write(buffer);
		await stream.close();
	} catch (err) {
		console.error(err);
	}
}

function setup_mmblkd_worker(msg) {

	if (mmblkd_worker)
		return;

	mmblkd_worker = new Worker("mmblkd.js");
    workers.push({addr: null, name: "mmblkd", worker: worker});
	mmblkd_head = msg.mmblkd_head;

	if (msg.kmemory && msg.mmblkd_head) {
		msg.cmd = "mmblkd_init_signal";
	} else {
		let init_msg = {cmd: "mmblkd_init_signal", mmblkd_head: msg.mmblkd_head};
		delete msg.mmblkd_head;
		mmblkd_worker.postMessage(init_msg);
	}

	if (fb_worker) {
		let ch = new MessageChannel();
		jsonrpc.send(fb_worker, "display_server.mmblkd_connect", {
			port: ch.port1,
			kmemory: msg.kmemory
		}, [ch.port1]).then(console.log, console.error);
		jsonrpc.send(mmblkd_worker, "mmblkd.connect", {
			port: ch.port2,
		}, [ch.port2]).then(console.log, console.error);
	}

	//worker3 = new Worker("mmblkd-tester.js");
	//worker3.addEventListener("message", lwp_message_handler);
}

// Debug

function setupMemoryDebugPanel(memory, statptr) {
	if (kmem_debug_data) {
		return;
	}

	let heapu32 = new Uint32Array(memory.buffer);
	let idx = statptr / 4;
	let ptrs = {};
	let info = {};
	kmem_debug_data = {};
	kmem_debug_data.memory = memory;
	kmem_debug_data.heapu32 = heapu32;
	kmem_debug_data.ptr = statptr;
	kmem_debug_data.statidx = idx;
	kmem_debug_data.info = info;
	kmem_debug_data.ptrs = ptrs;
	ptrs.rsvd_raw = idx++;
	ptrs.rsvd_pad = idx++;
	ptrs.rsvd_pgs = idx++;
	ptrs.pages_total = idx++;
	ptrs.pages_busy = idx++;
	ptrs.pages_pool = idx++;
	ptrs.pages_malloc = idx++;
	ptrs.malloc_busy = idx++;
	ptrs.malloc_free = idx++;

	for (let p in ptrs) {
		let ptr = ptrs[p];
		info[p] = 0;
	}

	memstatDiv = document.createElement("div");
	document.body.appendChild(memstatDiv);

	let cellmap = {};
	let table = document.createElement("table");
	let tbody, thead = document.createElement("thead");
	let tr = document.createElement("tr");
	let td, th = document.createElement("th");
	table.appendChild(thead);
	thead.appendChild(tr);
	tr.appendChild(th);
	th.colspan = 3;
	th.textContent = "Kernel Memory";
	tr = document.createElement("tr");
	th = document.createElement("th");
	thead.appendChild(tr);
	tr.appendChild(th);
	th.colspan = 3;
	th.textContent = "bytes";

	tbody = document.createElement("tbody");
	tr = document.createElement("tr");
	td = document.createElement("td");
	table.appendChild(tbody);
	tbody.appendChild(tr);
	tr.appendChild(td);
	td.textContent = "free";
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "0";
	cellmap.bytes_free = td;
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "bytes";

	tr = document.createElement("tr");
	td = document.createElement("td");
	tbody.appendChild(tr);
	tr.appendChild(td);
	td.textContent = "used";
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "0";
	cellmap.bytes_used = td;
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "bytes";

	thead = document.createElement("thead");
	tr = document.createElement("tr");
	th = document.createElement("th");
	table.appendChild(thead);
	thead.appendChild(tr);
	tr.appendChild(th);
	th.colspan = 3;
	th.textContent = "pages";

	tbody = document.createElement("tbody");
	tr = document.createElement("tr");
	td = document.createElement("td");
	table.appendChild(tbody);
	tbody.appendChild(tr);
	tr.appendChild(td);
	td.textContent = "free";
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "0";
	cellmap.pages_free = td;
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "pgs";

	tr = document.createElement("tr");
	td = document.createElement("td");
	tbody.appendChild(tr);
	tr.appendChild(td);
	td.textContent = "used";
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "0";
	cellmap.pages_used = td;
	td = document.createElement("td");
	tr.appendChild(td);
	td.textContent = "pgs";


	memstatDiv.appendChild(table);
	kmem_debug_data.cellmap = cellmap;

	requestAnimationFrame(render_mempanel);
}

function render_mempanel() {

	if (!kmem_debug_data)
		return;

	let heapu32 = kmem_debug_data.heapu32;
	let info = kmem_debug_data.info;
	let ptrs = kmem_debug_data.ptrs;
	let value, changed = false;

	for (let p in ptrs) {
		let ptr = ptrs[p];
		let old = info[p];
		let val = Atomics.load(heapu32, ptr);
		if (val != old) {
			info[p] = val;
			changed = true;
		}
	}

	if (changed) {
		let cellmap = kmem_debug_data.cellmap;
		let bytes_free = 0;
		let bytes_used = 0;
		let pages_free = info.pages_total - info.pages_busy;
		let pages_used = info.pages_busy;

		cellmap.bytes_free.textContent = bytes_free;
		cellmap.bytes_used.textContent = bytes_used;
		cellmap.pages_free.textContent = pages_free;
		cellmap.pages_used.textContent = pages_used;
	}

	requestAnimationFrame(render_mempanel);
}

// Display Server


dsrpc.addMethod("browserUI.setClipboard", function (target, params, transfer) {

});

dsrpc.addMethod("browserUI.getClipboard", function (target, params, transfer) {

});

dsrpc.addMethod("browserUI.getDataTransferFile", function (target, params, transfer) {

});

dsrpc.addMethod("browserUI.getDataTransferItem", function (target, params, transfer) {

});

/**
 * 
 * @param {MouseEvent} evt 
 * @returns {Object}
 */
function copyMouseEvent(evt) {
	let cpy = {};
	cpy.type = evt.type;
	cpy.isTrusted = evt.isTrusted;
	cpy.altKey = evt.altKey;
	cpy.button = evt.button;
	cpy.buttons = evt.buttons;
	cpy.clientX = evt.clientX;
	cpy.clientY = evt.clientY;
	cpy.composed = evt.composed;
	cpy.ctrlKey = evt.ctrlKey;
	cpy.detail = evt.detail;
	cpy.eventPhase = evt.eventPhase;
	cpy.defaultPrevented = evt.defaultPrevented;
	cpy.layerX = evt.layerX;
	cpy.layerY = evt.layerY;
	cpy.metaKey = evt.metaKey;
	cpy.movementX = evt.movementX;
	cpy.movementY = evt.movementY;
	cpy.offsetX = evt.offsetX;
	cpy.offsetY = evt.offsetY;
	cpy.pageX = evt.pageX;
	cpy.pageY = evt.pageY;
	cpy.screenX = evt.screenX;
	cpy.screenY = evt.screenY;
	cpy.shiftKey = evt.shiftKey;
	cpy.timeStamp = evt.timeStamp;
	cpy.which = evt.which;
	cpy.x = evt.x;
	cpy.y = evt.y;
	// evt.sourceCapabilities.firesTouchEvents
	
	return cpy;
}

function copyPointerEvent(evt) {
	let cpy = copyMouseEvent(evt); // PointerEvent extends MouseEvent
	cpy.altitudeAngle = evt.altitudeAngle;
	cpy.azimuthAngle = evt.azimuthAngle;
	cpy.height = evt.height;
	cpy.isPrimary = evt.isPrimary;
	cpy.pointerId = evt.pointerId;
	cpy.pointerType = evt.pointerType;
	cpy.pressure = evt.pressure
	cpy.tangentialPressure = evt.tangentialPressure;
	cpy.tiltX = evt.tiltX;
	cpy.tiltY = evt.tiltY;
	cpy.twist = evt.twist;
	cpy.width = evt.width;

	return cpy;
}

/**
 * 
 * @param {KeyboardEvent} evt 
 * @returns {Object}
 */
function copyKeyboardEvent(evt) {

	let cpy = {};
	cpy.type = evt.type;
	cpy.isTrusted = evt.isTrusted;
	cpy.altKey = evt.altKey;
	cpy.charCode = evt.charCode;
	cpy.code = evt.code;
	cpy.composed = evt.composed;
	cpy.ctrlKey = evt.ctrlKey;
	cpy.defaultPrevented = evt.defaultPrevented;
	cpy.detail = evt.detail;
	cpy.eventPhase = evt.eventPhase;
	cpy.isComposing = evt.isComposing;
	cpy.key = evt.key;
	cpy.keyCode = evt.keyCode
	cpy.location = evt.location;
	cpy.metaKey = evt.metaKey;
	cpy.repeat = evt.repeat;
	cpy.shiftKey = evt.shiftKey;
	cpy.timeStamp = evt.timeStamp;
	cpy.which = evt.which;

	return cpy;
}

/**
 * 
 * @param {WheelEvent} evt 
 * @returns {Object}
 */
function copyWheelEvent(evt) {
	let cpy = {};
	cpy.type = evt.type;
	cpy.isTrusted = evt.isTrusted;
	cpy.altKey = evt.altKey;
	cpy.button = evt.button;
	cpy.buttons = evt.buttons;
	cpy.clientX = evt.clientX;
	cpy.clientY = evt.clientY;
	cpy.composed = evt.composed;
	cpy.ctrlKey = evt.ctrlKey;
	cpy.deltaMode = evt.deltaMode;
	cpy.deltaY = evt.deltaY;
	cpy.deltaX = evt.deltaX;
	cpy.deltaZ = evt.deltaZ;
	cpy.detail = evt.detail;
	cpy.eventPhase = evt.eventPhase;
	cpy.defaultPrevented = evt.defaultPrevented;
	cpy.layerX = evt.layerX;
	cpy.layerY = evt.layerY;
	cpy.metaKey = evt.metaKey;
	cpy.movementX = evt.movementX;
	cpy.movementY = evt.movementY;
	cpy.offsetX = evt.offsetX;
	cpy.offsetY = evt.offsetY;
	cpy.pageX = evt.pageX;
	cpy.pageY = evt.pageY;
	cpy.screenX = evt.screenX;
	cpy.screenY = evt.screenY;
	cpy.shiftKey = evt.shiftKey;
	cpy.timeStamp = evt.timeStamp;
	cpy.wheelDelta = evt.wheelDelta;
	cpy.wheelDeltaX = evt.wheelDeltaX;
	cpy.wheelDeltaY = evt.wheelDeltaY;
	cpy.which = evt.which;
	cpy.x = evt.x;
	cpy.y = evt.y;
	
	return cpy;
}

// let model = pushDragDataTransfer(evt.dataTransfer);
// DataTransferItem(s) is only available during the event call frame, once exiting the event call frame the
// .kind & .type properties are set to empty string and.
// Neiter is the DataTransfer object a transferable type, 
/**
 * 
 * @param {DragEvent} evt 
 * @returns {Object}
 */
function copyDragEvent(evt) {
	let cpy = {};
	cpy.type = evt.type;
	cpy.isTrusted = evt.isTrusted;
	cpy.altKey = evt.altKey;
	cpy.button = evt.button;
	cpy.buttons = evt.buttons;
	cpy.clientX = evt.clientX;
	cpy.clientY = evt.clientY;
	cpy.composed = evt.composed;
	cpy.ctrlKey = evt.ctrlKey;
	cpy.detail = evt.detail;
	cpy.eventPhase = evt.eventPhase;
	cpy.defaultPrevented = evt.defaultPrevented;
	cpy.layerX = evt.layerX;
	cpy.layerY = evt.layerY;
	cpy.metaKey = evt.metaKey;
	cpy.movementX = evt.movementX;
	cpy.movementY = evt.movementY;
	cpy.offsetX = evt.offsetX;
	cpy.offsetY = evt.offsetY;
	cpy.pageX = evt.pageX;
	cpy.pageY = evt.pageY;
	cpy.screenX = evt.screenX;
	cpy.screenY = evt.screenY;
	cpy.shiftKey = evt.shiftKey;
	cpy.timeStamp = evt.timeStamp;
	cpy.which = evt.which;
	cpy.x = evt.x;
	cpy.y = evt.y;

	let dataTransfer = evt.dataTransfer;
	let dfiles = [];
	let ditems = [];
	let dtypes = [];
	let obj = {};
	obj.dropEffect = dataTransfer.dropEffect;
	obj.effectAllowed = dataTransfer.effectAllowed;
	obj.files = dfiles;
	obj.items = ditems;
	obj.types = dtypes;

	if (dataTransfer.files.length > 0) {
		let files = dataTransfer.files;
		let len = files.length;
		for (let i = 0; i < len; i++) {
			let file = files[i];
			let itemId = dataTransferItemId++;
			dfiles.push({id: itemId, name: file.name});
			dataTransferItemMap.set(itemId, file);
		}
	}

	if (dataTransfer.items.length > 0) {
		let items = dataTransfer.items;
		let len = items.length;
		for (let i = 0; i < len; i++) {
			let item = items[i];
			let itemId = dataTransferItemId++;
			ditems.push({id: itemId, kind: item.kind, type: item.type});
			dataTransferItemMap.set(itemId, item);
		}
	}

	if (dataTransfer.types.length > 0) {
		let types = dataTransfer.types;
		let len = types.length;
		for (let i = 0; i < len; i++) {
			dtypes.push(types[i]);
		}
	}

	cpy.dataTransfer = obj;
	cpy._dataTransfer = dataTransfer;
	
	return cpy;
}

function pushDragDataTransfer(evt) {
	let newModel = copyDragEvent(evt);
	let dnd = {model: newModel, dataTransfer: evt.dataTransfer};
	dragDataTransferStack.push(dnd);

	if (dragDataTransferStack.length > 5) {
		let dnd = dragDataTransferStack.shift();
		// clear mapping
		let dataTransfer = dnd.model.dataTransfer;
		if (dataTransfer.files.length > 0) {
			let files = dataTransfer.files;
			let len = files.length;
			for (let i = 0; i < len; i++) {
				let itemId = files[i].itemId;
				dataTransferItemMap.delete(itemId);
			}
		}

		if (dataTransfer.items.length > 0) {
			let items = dataTransfer.items;
			let len = items.length;
			for (let i = 0; i < len; i++) {
				let itemId = items[i].itemId;
				dataTransferItemMap.delete(itemId);
			}
		}
	}

	return newModel;
}

function clearDragDataTransfer(evt) {

	while (dragDataTransferStack.length > 0) {
		let dnd = dragDataTransferStack.pop();
		// clear mapping
		let dataTransfer = dnd.model.dataTransfer;
		if (dataTransfer.files.length > 0) {
			let files = dataTransfer.files;
			let len = files.length;
			for (let i = 0; i < len; i++) {
				let itemId = files[i].itemId;
				dataTransferItemMap.delete(itemId);
			}
		}

		if (dataTransfer.items.length > 0) {
			let items = dataTransfer.items;
			let len = items.length;
			for (let i = 0; i < len; i++) {
				let itemId = items[i].itemId;
				dataTransferItemMap.delete(itemId);
			}
		}
	}
}

function onDisplayServerMessage(evt) {

	let msg = evt.data;
	if (msg.jsonrpc === JSONRPC_HEAD) {
		ret = dsrpc._handleMessage(evt);
		return;
	}
}
/**
 * 
 * @returns 
 */
function setupDisplayServer() {
	if (fb_worker)
		return;

	/** @type {HTMLDivElement} */
	let canvasContainer;
	/** @type {HTMLCanvasElement} */
	let canvas;
	/** @type {OffscreenCanvas} */
	let offscreen;
	let keyCharMap = {};
	let canvasWidth = 0;
	let canvasHeight = 0;
	let touchEvents = false;

	if (canvasQuerySelector) {
		canvas = document.querySelector(canvasQuerySelector);
		if (!(canvas instanceof HTMLCanvasElement)) {
			throw new TypeError("Not a HTMLCanvasElement");
		}
		canvasContainer = canvas.parentElement;
		canvasWidth = canvas.width;
		canvasHeight = canvas.height;
	} else {
		canvas = document.createElement("canvas");
		canvasContainer = document.createElement("div");
		canvasContainer.classList.add("primary-viewport")
		canvasContainer.appendChild(canvas);
		document.body.appendChild(canvasContainer);
		canvasWidth = canvas.width;
		canvasHeight = canvas.height;
	}

	offscreen = canvas.transferControlToOffscreen();

	fb_worker = new Worker("display-server.js");
	fb_worker.addEventListener("message", onDisplayServerMessage);

	canvas.addEventListener("mousedown", function(evt) {

		jsonrpc.notify(fb_worker, "ui_event", evt instanceof PointerEvent ? copyPointerEvent(evt) : copyMouseEvent(evt));
	});

	canvas.addEventListener("mouseup", function(evt) {
		jsonrpc.notify(fb_worker, "ui_event", evt instanceof PointerEvent ? copyPointerEvent(evt) : copyMouseEvent(evt));
	});

	canvas.addEventListener("click", function(evt) {
		console.log(evt);
		evt.preventDefault();
		return false;
	});

	canvas.addEventListener("dblclick", function(evt) {
		console.log(evt);
		evt.preventDefault();
		return false;
	});

	canvas.addEventListener("mousemove", function(evt) {
		jsonrpc.notify(fb_worker, "ui_event", copyMouseEvent(evt));
	});

	canvas.addEventListener("wheel", function(evt) {
		jsonrpc.notify(fb_worker, "ui_event", copyMouseEvent(evt));
        return false;
	});

	// disable context menu for canvas..
	canvas.addEventListener("contextmenu", function(evt) {
		evt.preventDefault();
		return false;
	});

	// Drag and Drop

	canvas.addEventListener("drag", function(evt) {
		console.log(evt);
		let model = pushDragDataTransfer(evt);
		lastDNDDataTransfer = evt.dataTransfer;
		lastDNDDataTransferModel = cpy;
		jsonrpc.notify(fb_worker, "ui_event", model, [evt.dataTransfer]);
	});

	canvas.addEventListener("dragend", function(evt) {
		console.log(evt);
		let model = pushDragDataTransfer(evt);
		jsonrpc.notify(fb_worker, "ui_event", model, [evt.dataTransfer]);
	});

	canvas.addEventListener("dragenter", function(evt) {
		console.log(evt);
		let model = copyDragEvent(evt);
		jsonrpc.notify(fb_worker, "ui_event", model, [evt.dataTransfer]);
	});

	canvas.addEventListener("dragleave", function(evt) {
		console.log(evt);
		let model = pushDragDataTransfer(evt);
		jsonrpc.notify(fb_worker, "ui_event", model, [evt.dataTransfer]);
	});

	canvas.addEventListener("dragover", function(evt) {
		let model = pushDragDataTransfer(evt);
		jsonrpc.notify(fb_worker, "ui_event", model, [evt.dataTransfer]);
		// prevent default to allow drop
      	evt.preventDefault();
	});

	canvas.addEventListener("dragstart", function(evt) {
		// Not sure about how well the canvas works when proxying a dragstart
		// 
		// TODO: To enable a HTMLCanvasElement to be able to initiate such event the draggable
		// attribute needs to be set to true, there might be chance to use 
		// evt.preventDefault() to avoid starting a drag where there is no
		// dragable item under the mouse within the GnuStep view realm. What the
		// browser will generate as drag-image must be replaced by calling 
		// evt.dataTransfer.setDragImage(element, x, y) as it would otherwise
		// be dragging the main canvas. The mdn docs mention that <img> / <canvas>
		// tag is the recomended element to use. I thing however that it is a
		// single render use.
		//
		// However the issue is that it could become a race between mousedown & dragstart.
		//
		// Another alternative would be map the regions from which a dragstart should be fired,
		// but this does not exists in GnuStep ATM. And such region mapping would limited to rectangular regions.
	});

	// TODO: to bridge files to only thing that makes sense is to handle these trough hyper-io thread.
	canvas.addEventListener("drop", function(evt) {
		console.log(evt);
		let model = pushDragDataTransfer(evt);
		jsonrpc.send(fb_worker, "ui_event", model, [evt.dataTransfer]).then(function() {
			clearDragDataTransfer();
		});
	});

	// Keyboard and Text-Input

	window.addEventListener("keydown", function(evt) {
		//TODO: use evt.getModifierState("") to map other modifiers.
		// https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/getModifierState#modifier_keys_on_firefox
		// "CapsLock", "NumLock", "AltGraph", "ScrollLock"	
		jsonrpc.notify(fb_worker, "ui_event", copyKeyboardEvent(evt));
		evt.preventDefault();
	});

	// The "keypress" event is basically the same as a keyup but also fires for repeat. 

	window.addEventListener("keyup", function(evt) {
		jsonrpc.notify(fb_worker, "ui_event", copyKeyboardEvent(evt));
		evt.preventDefault();
	});

	// check other projects where input proxying is implemented.
	// atleast from what i remember from the Retro-Platform's impl
	// of such there where a special behaivor when any responder which
	// handled text where the first responder, in which case it set the focus
	// a hidden <input> 

	var keyboard = navigator.keyboard;
	keyboard.getLayoutMap().then(function(keyboardLayoutMap) {
		console.log(keyboardLayoutMap);

		/*for (let key in keys) {
			let val = keyboardLayoutMap.get(key);
			console.log("key: '%s' value: '%s'", key, val);
		}*/
		keyboardLayoutMap.forEach(function(chr, key) {
			keyCharMap[key] = chr;
		});

		console.log(keyCharMap);
	});

	// only some runtimes have this events..
	if (typeof navigator.keyboard.addEventListener == "function") {
		navigator.keyboard.addEventListener("layoutchange", function() {
			// Update user keyboard map settings
			navigator.keyboard.getLayoutMap().then(function(keyboardLayoutMap) {

				let keyCharMap = {};
				keyboardLayoutMap.forEach(function(chr, key) {
					keyCharMap[key] = chr;
				});
		
				jsonrpc.notify(fb_worker, "ui_event", {type: "keyboard.layoutchange", keyCharMap: keyCharMap})
			});
		});
	}
	
	//window.addEventListener("resize", domWindowOnResize);
		
	document.addEventListener("visibilitychange", function(evt) {
		jsonrpc.notify(fb_worker, "ui_event", {type: "visibilitychange", visibilityState: document.visibilityState});
	});

	function domWindowOnOrientationChange(evt) {
		console.log("screen.orientation.type: " + screen.orientation.type);
		console.log("screen.orientation.angle: " + screen.orientation.angle);
		/*
		screen.orientation.type:  portrait-primary
		screen.orientation.angle: 0
		screen.orientation.type:  landscape-primary
		screen.orientation.angle: 90
		screen.orientation.type:  landscape-primary
		screen.orientation.angle: 90
		 */
		jsonrpc.notify(fb_worker, "ui_event", {type: "orientationchange", orientationData: {type: screen.orientation.type, angle: screen.orientation.angle}});
	}

	
	if(screen.orientation && typeof screen.orientation.addEventListener == "function"){
		screen.orientation.addEventListener("change", domWindowOnOrientationChange);
	}else{
		window.addEventListener("orientationchange", domWindowOnOrientationChange);
	}

	let resolutionMediaQuery = window.matchMedia("(resolution: " + window.devicePixelRatio + "dppx)");
	resolutionMediaQuery.addListener(function domWindowOnPixelRatioChange(evt) {
		// May happen if the user drags the browser-window/tab to a screen with another pixel dencity.
		// 
		// https://developer.mozilla.org/en-US/docs/Web/API/Window/devicePixelRatio#Monitoring_screen_resolution_or_zoom_level_changes

		// @todo 1. Update the backingstore pixel-ration for the backing of the current set of active scenes.
		//       2. In some cases the cached preview of inactive scenes may need to be optimized.
		let newPixelRatio = window.devicePixelRatio;
		console.log("resolution: (new: " + newPixelRatio + ") MediaQuery did trigger change event");
		console.log(evt);
		jsonrpc.notify(fb_worker, "ui_event", {type: "devicepixelratiochange", devicePixelRatio: newPixelRatio});
	});

	let prefersColorSchemeMediaQuery = window.matchMedia("(prefers-color-scheme: dark)");
	prefersColorSchemeMediaQuery.addListener(function(evt) {
		// prefers-color-scheme: light
		// prefers-color-scheme: dark
		// prefers-color-scheme: no-preference
		let prefersColorScheme = null;

		// https://web.dev/prefers-color-scheme/
		
		// @todo 1. Invalidate and update traits for the current set of active scenes
		//       2. Needs implementation to allow scenes in the app-switcher to refresh their UI.
		
		if (evt.matches) {
			prefersColorScheme = "dark";
		} else if (window.matchMedia("(prefers-color-scheme: light)")) {
			prefersColorScheme = "light";
		} else {
			prefersColorScheme = "no-preference";
		}

		console.log("prefers-color-scheme: (new: '%s') MediaQuery did trigger change event", prefersColorScheme);
		console.log(evt);
		jsonrpc.notify(fb_worker, "ui_event", {type: "preferscolorschemechange", prefersColorScheme: prefersColorScheme});
	});

	if (touchEvents) {
		
		canvas.addEventListener("touchstart", function(evt) {

		});

		canvas.addEventListener("touchmove", function(evt) {

		});
		
		canvas.addEventListener("touchend", function(evt) {

		});
		
		canvas.addEventListener("touchcancel", function(evt) {

		});

		canvas.addEventListener("touchstart", function(evt) {

		});
	}

	/**
	 * 
	 * @param {ResizeObserverEntry[]} entries 
	 * @param {ResizeObserver} observer 
	 */
	function onCanvasParentResize(entries, observer) {
		for (const entry of entries) {
			if (entry != canvasContainer)
				continue;

			let contentBoxSize = entry.contentBoxSize[0];
			let width = contentBoxSize.inlineSize;
			let height = contentBoxSize.blockSize;

			if (width != canvasWidth || height != canvasHeight) {
				jsonrpc.notify(fb_worker, "ui_event", {type: "viewportResize", viewportData: {width: width, height: height}});
				canvasWidth = width;
				canvasHeight = height;
			}
			
			console.log("%o %o %o", entry.borderBoxSize, entry.contentBoxSize, entry.devicePixelContentBoxSize);
		}
	}

	const resizeObserver = new ResizeObserver(onCanvasParentResize);

	resizeObserver.observe(canvasContainer);

	{
		// What's in this closure is not needed for the event closures..
		let transfer = [offscreen];
		let opts = {};
		let init_params = {
			offscreenCanvas: offscreen,
			devicePixelRatio: window.devicePixelRatio,
			keyCharMap: null,
			capabilities: null,
			prefersColorScheme: "no-preference",
			options: opts,
		};
		let init_msg = {
			jsonrpc: "2.0",
			id: "abc123",
			method: "display_server_init",
			params: init_params,
			_transfer: transfer
		};
		if (mmblkd_worker) {
			let ch = new MessageChannel();
			opts.mmblkd_port = ch.port1;
			transfer.push(ch.port1);

			jsonrpc.send(mmblkd_worker, "mmblkd.connect", {port: ch.port2}, [ch.port2]).then(console.log, console.error);
		}

		if (__dsrpc_server_head) {
			opts.__dsrpc_server_head;
		}

		init_params.capabilities = {
			EditContext: (typeof window.EditContext == "function"),
			// https://developer.mozilla.org/en-US/docs/Web/API/EyeDropper
			EyeDropper: (typeof window.EyeDropper == "function"),
		};

		let initialColorScheme;
		if (window.matchMedia("(prefers-color-scheme: dark)")) {
			init_params.prefersColorScheme = "dark";
		} else if (window.matchMedia("(prefers-color-scheme: light)")) {
			init_params.prefersColorScheme = "light";
		} else {
			init_params.prefersColorScheme = "no-preference";
		}

		if (navigator.keyboard) {
			
			navigator.keyboard.getLayoutMap().then(function(keyboardLayoutMap) {

				let keyCharMap = {};
				keyboardLayoutMap.forEach(function(chr, key) {
					keyCharMap[key] = chr;
				});

				init_params.keyCharMap = keyCharMap;
				fb_worker.postMessage(init_msg, transfer);

			});

		} else {
			init_params.keyCharMap = {};
			fb_worker.postMessage(init_msg, transfer);
		}

		windowManagerWorker = fb_worker;
	}
}

setupDisplayServer();

/**
 * 
 * @param {LaunchParam} launchParam 
 */
function launchQueue_handler(launchParam) {
    console.log(launchParam);
}

if (window.launchQueue) {
    window.launchQueue.setConsumer(launchQueue_handler);
}

window.addEventListener("beforeunload", function(evt) {
	
	worker.postMessage({
		cmd: "beforeunload"
	});

	return "This dialoge pops up in hopes of the disk-image not getting corrupt..";
});











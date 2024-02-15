
// @todo Some type of messaging format is needed to transfer messages that should be responded to.
// 		 similar to jsonrpc.

let doneInit = false;
let mmblkdPort;
let offscreenCanvas;
let primaryViewport;
let viewports = [];
let eventHandlers = {
	'mousedown': console.log
};

console.log("spawned display-server.js thread");

importScripts("jsonrpc.js");

function addEvent(event, handler) {
	eventHandlers[event] = handler;
}

function createViewport(offscreen) {
	let obj = {};
	obj.canvas = offscreen;
	obj.context = offscreen.getContext("2d");
	obj.width = offscreen.width;
	obj.height = offscreen.height;
	let ctx = obj.context;
	let num = viewports.length + 1;
	ctx.fillText("Viewport # " + num, 50, 90);
	return obj;
}

async function display_server_init(target, params, transfer) {

	if (doneInit)
		throw new Error("INVALID_INIT");

	let options = params.options;
	let iosurfaceIPCHead = options.iosurfaceIPCHead;

	if (options.global_ipc_port) {
		addGlobalIPCMessagePort(options.global_ipc_port);
		global_ipc_port = options.global_ipc_port;
	}

    // TODO: setup listeners to the iosurfaceIPCHead

	primaryViewport = createViewport(params.offscreenCanvas);
	//viewports.push(primaryViewport);
	
	let res, channel = new MessageChannel();
	try {
		res = await jsonrpc.send(self, "mmblkd.connect", {port: channel.port1}, [channel.port1]);
	} catch (err) {
		console.error(err);
		throw err;
	}

	mmblkdPort = channel.port2;
	mmblkdPort.start();
	mmblkdPort.addEventListener("message", mmblkdOnMessage);

	let caps = {};
	caps.version = "1.0";
	return caps;
}

function handle_ui_event(target, event, transfer) {
	let ret, type = event.type;
	let handler = eventHandlers[type];
	if (typeof handler !== "function")
		return;
	
	ret = handler(event);
}

addEvent("mousedown", function (evt) {

});

addEvent("mouseup", function (evt) {

});

addEvent("click", function (evt) {

});

addEvent("dblclick", function (evt) {

});

addEvent("mousemove", function (evt) {

});

addEvent("wheel", function (evt) {

});

// Drag and Drop

addEvent("drag", function (evt) {

});

addEvent("dragend", function (evt) {

});

addEvent("dragenter", function (evt) {

});

addEvent("dragleave", function (evt) {

});

addEvent("dragover", function (evt) {
	console.log(evt);
});

addEvent("dragstart", function (evt) {

});

addEvent("drop", function (evt) {

});

// Keyboard and Text-Input

addEvent("keydown", function (evt) {

});

addEvent("keyup", function (evt) {

});

addEvent("keyboard.layoutchange", function (evt) {

});

addEvent("textinput", function (evt) {

});

addEvent("copy", function (evt) {

});

addEvent("cut", function (evt) {

});

addEvent("paste", function (evt) {

});

//

addEvent("viewportchange", function (evt) {

});

addEvent("visibilitychange", function (evt) {

});

addEvent("orientationchange", function (evt) {

});

addEvent("devicepixelratiochange", function (evt) {

});

addEvent("preferscolorschemechange", function (evt) {

});


// JSON RPC

const jsonrpc = new JSONRPC();

jsonrpc.addMethod("display_server_init", display_server_init);
jsonrpc.addMethod("ui_event", handle_ui_event);

// 

function mmblkdOnMessage (evt) {

	console.log(evt);

	let ret, msg = evt.data;

	if (msg.jsonrpc === JSONRPC_HEAD) {
		ret = jsonrpc._handleMessage(evt);
		return;
	}
};

// message in self

self.addEventListener("message", function(evt) {

	let ret, msg = evt.data;

	if (msg.jsonrpc === JSONRPC_HEAD) {
		ret = jsonrpc._handleMessage(evt);
		return;
	}

	console.log(evt);
	
	if (typeof msg.cmd == "string") {
		let cmd = msg.cmd;
		if (cmd == "display_server_init") {
			display_server_init(msg);
		} else if (cmd == "ui_event") {
			handle_ui_event(msg.event);
		}
	}
});





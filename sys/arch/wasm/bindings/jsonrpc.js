
const JSONRPC_HEAD = "2.0";

function JSONRPC() {

	const port = globalThis;
	const resp_map = {};
	const methods = {};

	const MESSAGE_ID_CHARS = "0123456789abcdef";
	const MESSAGE_ID_LENGTH = 16;

	function generateMsgId() {
	    let len = MESSAGE_ID_LENGTH;
	    let chars = MESSAGE_ID_CHARS;
	    let cc = chars.length;
	    let ci, uid;
	   	do {
	   		uid = "";
		    for(let i = 0;i < len;i++){
		        ci = Math.random() * cc | 0;
		        uid += chars[ci];
		    }
		} while (resp_map.hasOwnProperty(uid))

	    return uid;
	}

	function getJsonRPCErrorObject(error) {
		let code;
		let message;
		if (typeof error == "object" && error !== null) {
			code = error.code;
			message = error.message;
		}

		return {code: code, message: message};
	}

	this._handleMessage = function jsonrpc_handle(evt) {

		let msg = evt.data;
		let target = evt.target;
		if (msg.jsonrpc !== JSONRPC_HEAD) {
			return false;
		}

		let hasId = typeof msg.id === "string" || Number.isInteger(msg.id);

		if (typeof msg.method == "string") {
			// handle request
			let method = msg.method;
			let retval, params = msg.params;
			let handler = methods[method];
			let transfer = [];

			if (typeof handler != "function") {

				// handling forward case
				if ((typeof handler == "object" && handler !== null) && handler.isForward === true) {
					let port = handler.target;
					let _transfer = msg._transfer;
					let hasTransfer = _transfer && Array.isArray(_transfer) && _transfer.length > 0;
					

					if (hasId) {
						let orgId, newId = generateMsgId();
						orgId = msg.id;
						resp_map[newId] = {isForward: true, target: port, sender: target, orgId: orgId};
					}

					if (hasTransfer) {
						port.postMessage(msg, _transfer);
					} else {
						port.postMessage(msg);
					}
				} else if (hasId) {
					target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, error: {code: -32601, message: "Method not found"}});
				}
				return;
			}

			if (Array.isArray(params)) {

				params.unshift(target);
				params.unshift(transfer);

				try {
					retval = handler.apply(null, params);
				} catch (err) {
					if (hasId) {
						target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, error: getJsonRPCErrorObject(err)});
						return;
					}
				}

			} else if (typeof params == "object" && params !== null) {

				try {
					retval = handler(target, params, transfer);
				} catch (err) {
					if (hasId) {
						target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, error: getJsonRPCErrorObject(err)});
						return;
					}
				}
			}

			// 
			if (hasId) {
				if (retval instanceof Promise) {

					retval.then(function (res) {
						if (transfer.length > 0) {
							target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, result: res}, transfer);
						} else {
							target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, result: res});
						}
					}, function (err) {
						target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, error: getJsonRPCErrorObject(err)});
					});

				} else {
					if (transfer.length > 0) {
						target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, result: retval}, transfer);
					} else {
						target.postMessage({jsonrpc: JSONRPC_HEAD, id: msg.id, result: retval});
					}
				}
			}

		} else if (hasId) {
			// handle response
			let resp, id = msg.id;
			if (!resp_map.hasOwnProperty(id)) {
				return;
			}

			resp = resp_map[id];

			if (resp.target !== target) {
				console.error("message was not responsed to by same target");
			}

			delete resp_map[id];

			if (resp.isForward === true) {
				let sender = resp.sender;
				let orgId = resp.orgId;
				msg.id = orgId;
				sender.postMessage(msg);
			} else if (msg.hasOwnProperty("result")) {
				resp.resolve(msg.result);
			} else if (msg.hasOwnProperty("error")) {
				resp.reject(msg.error);
			} else {
				console.error("Invalid response format");
			}
		}
	}

	this.addPort = function(port) {

	}

	this.addMethod = function(method, handler) {
		if (typeof method != "string" || typeof handler != "function")
			throw new TypeError("INVALID_TYPE");

		methods[method] = handler;
	}

	this.removeMethod = function(method) {
		if (typeof method != "string")
			throw new TypeError("INVALID_TYPE");
		
		delete methods[method];
	}

	this.forward = function (method, target) {

		let obj = {isForward: true, target: target};
		methods[method] = obj;
	}

	this.send = function(target, method, params, transfer) {

		let msgId = generateMsgId();
		let hasTransfer = transfer && Array.isArray(transfer) && transfer.length > 0;
		let resolveFn, rejectFn, p;
		let msg = {};
		msg.jsonrpc = JSONRPC_HEAD;
		msg.id = msgId;
		msg.method = method;
		msg.params = params;
		msg._transfer = hasTransfer ? transfer : null;

		p = new Promise(function(resolve, reject) {
			resolveFn = resolve;
			rejectFn = reject;
		});

		resp_map[msgId] = {
			resolve: resolveFn,
			reject: rejectFn,
			target: target,
		};

		if (hasTransfer) {
			target.postMessage(msg, transfer);
		} else {
			target.postMessage(msg);
		}

		return p;
	}

	this.notify = function(target, method, params, transfer) {

		let hasTransfer = transfer && Array.isArray(transfer) && transfer.length > 0;
		let msg = {};
		msg.jsonrpc = JSONRPC_HEAD;
		msg.method = method;
		msg.params = params;
		msg._transfer = hasTransfer ? transfer : null;

		if (hasTransfer) {
			target.postMessage(msg, transfer);
		} else {
			target.postMessage(msg);
		}
	}
}
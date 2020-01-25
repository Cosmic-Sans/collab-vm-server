const runtime = require("runtime");
import Module from "collab-vm-web-app";

import $ from "jquery";

import "semantic-ui";
import "semantic-ui.css";

const rootPath = __webpack_public_path__;
if (!(window.location.pathname + "/").startsWith(rootPath)) {
  console.error(`Content must be located at '${rootPath}'.`);
}
$("#menu > a").click(e => {
  const url = e.currentTarget.getAttribute("href");
  if (!url.startsWith(rootPath)) {
    return;
  }
  e.preventDefault();
  setUrl(url);
});

if (__DEV__) {
  require("expose-loader?common!common");
  require("expose-loader?$!jquery");
  require("expose-loader?em!collab-vm-web-app");
  console.debug("__DEV__ enabled");
}

const routers = [];
const registerUrlRouter = router => {
  routers.push(router);
};
const findRoute = () => {
  $("[id^='page-']").hide();
  const path = getPath();
  if (!routers.find(router => router(path) !== false)) {
    const page = document.getElementById("page-" + path.substr(1));
    if (page) {
      $(page).show();
    } else {
      $("#not-found").show();
    }
  }
};
const setUrl = url => {
  window.history.pushState({}, "", rootPath.substr(0, rootPath.length - 1) + url);
  findRoute();
};
const getPath = () => {
    const path = window.location.pathname.substr(rootPath.length - 1);
    return (path ? path : "/").replace(/\/$/, "").replace(/\.html$/, "");
};
window.addEventListener("popstate", event => {
  findRoute();
});
let serializer = {connected: false};
const getSocket = () => serializer;

const messageHandlers = {};
const addMessageHandlers = handlers => {
  Object.assign(messageHandlers, handlers);
};

const setClassProperties = (obj, props) => {
  Object.entries(props).forEach(keyVal =>
    obj["set" + keyVal[0][0].toUpperCase() + keyVal[0].substr(1)](keyVal[1]));
  return obj;
};
const createObject = (name, properties) => {
  return setClassProperties(new Module[name], properties || {});
};
Array.prototype.toVector = function(name) {
  const vector = createObject(name);
  this.forEach(element => vector.push_back(element));
  return vector;
}
const saveSessionInfo = (sessionId, username) => {
  localStorage["sessionId"] = sessionId;
  localStorage["username"] = username;
};
const loadSessionInfo = () => {
  return {sessionId: localStorage["sessionId"], username: localStorage["username"]};
};
let common;
export {
  registerUrlRouter,
  setUrl,
  getPath,
  getSocket,
  addMessageHandlers,
  createObject,
  saveSessionInfo,
  loadSessionInfo,
  common
};

runtime.onRuntimeInitialized(() => {

  common = Module.Constants;

  serializer = Object.assign(Module.Serializer.implement({
    onMessageReady: message =>
    {
      webSocket.send(message);
    }
  }), serializer);

  $(() => {
    findRoute();
  });

  let webSocket;
  const deserializer = Module.Deserializer.implement(messageHandlers);

  function connectWebSocket() {
    webSocket = new WebSocket(WEBSOCKET_ADDRESS);
    webSocket.binaryType = "arraybuffer";
    let connected = false;
    webSocket.onopen = () => {
      connected = true;
      serializer.connected = true;
      if (serializer.onSocketConnect) {
        serializer.onSocketConnect();
      }
    };
    webSocket.onmessage = ({data}) => deserializer.deserialize(data);
    webSocket.onclose = () => {
      if (connected) {
        if (serializer.onSocketDisconnect) {
          serializer.onSocketDisconnect();
        }
        connected = false;
        connectWebSocket();
      } else {
        setTimeout(connectWebSocket, 5000);
      }
    };
  }

  connectWebSocket();
});

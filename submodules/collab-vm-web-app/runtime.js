module.exports.locateFile = file => {
  if (file === "collab-vm-web-app.wasm") {
    return require("collab-vm-web-app.wasm");
  }
  return file;
};
let runtimeInitializedHandler;
module.exports.isRuntimeInitialized = false;
module.exports.onRuntimeInitialized =
  handler => {
    if (handler) {
      if (module.exports.isRuntimeInitialized) {
        handler();
      } else {
        runtimeInitializedHandler = handler;
      }
    } else {
      module.exports.isRuntimeInitialized = true;
      if (runtimeInitializedHandler) {
        runtimeInitializedHandler();
      }
    }
  };

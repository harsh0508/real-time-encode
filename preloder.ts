import { contextBridge, ipcRenderer } from "electron"

contextBridge.exposeInMainWorld("api", {
  startEngine: () => ipcRenderer.send("start-engine"),
  stopEngine: () => ipcRenderer.send("stop-engine"),
  onCppLog: (callback: (msg: string) => void) => {
    ipcRenderer.on("cpp-log", (_event, msg: string) => callback(msg))
  }
})
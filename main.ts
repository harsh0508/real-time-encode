import { app, BrowserWindow, ipcMain, session } from "electron";
import path from "path";
import { fileURLToPath } from "node:url";
import { spawn } from "child_process";
import type { ChildProcessWithoutNullStreams } from "child_process";

let cppProcess: ChildProcessWithoutNullStreams | null = null;
let win: BrowserWindow | null = null;

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function getCppEnginePath() {
  const exeName = process.platform === "win32"
    ? "cpp_engine.exe"
    : "cpp_engine";

  return path.join(process.cwd(), "build", exeName);
}

function createWindow() {
  win = new BrowserWindow({
    width: 1200,
    height: 800,
    webPreferences: {
      // IMPORTANT:
      // You said your file is named preloder.ts.
      // After TypeScript build, Electron needs the compiled JS file: preloder.js
     preload: path.join(process.cwd(), "preloder.js"),

      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  session.defaultSession.setPermissionRequestHandler(
    (_webContents, permission, callback) => {
      if (permission === "media") {
        callback(true);
      } else {
        callback(false);
      }
    }
  );

  win.loadFile(
    path.join(__dirname, "../real-time-encode/renderer/index.html")
  );

  // Optional: open DevTools while debugging preload/window.api
  // win.webContents.openDevTools(); 

  win.on("closed", () => {
    win = null;
  });
}

ipcMain.handle("cpp:start", async () => {
  if (cppProcess) {
    return {
      ok: true,
      message: "cpp_engine already running",
    };
  }

  const cppPath = getCppEnginePath();
  const modelPath = path.join(
    process.cwd(),
    "onnx_model",
    "improved_fight_model.onnx"
  );

  console.log("Starting C++ engine from:", cppPath);

  // cppProcess = spawn(cppPath, ["--rtmp", "--model", modelPath], {
  //   cwd: process.cwd(),
  //   stdio: ["pipe", "pipe", "pipe"],
  // });

  cppProcess = spawn(cppPath, [ "--model", modelPath], {
    cwd: process.cwd(),
    stdio: ["pipe", "pipe", "pipe"],
  });

  if (!cppProcess || cppProcess.pid === undefined) {
    console.error("Failed to start C++ engine");
    cppProcess = null;

    return {
      ok: false,
      message: "Failed to start C++ engine",
    };
  }

  cppProcess.stdout.on("data", (data) => {
    const message = data.toString();
    console.log("[cpp_engine]", message);

    if (win && !win.isDestroyed()) {
      win.webContents.send("cpp:log", message);
    }
  });

  cppProcess.stderr.on("data", (data) => {
    const message = data.toString();
    console.error("[cpp_engine error]", message);

    if (win && !win.isDestroyed()) {
      win.webContents.send("cpp:error", message);
    }
  });

  cppProcess.on("error", (error) => {
    console.error("cpp_engine spawn error:", error);

    cppProcess = null;

    if (win && !win.isDestroyed()) {
      win.webContents.send("cpp:error", error.message);
    }
  });

  cppProcess.on("close", (code) => {
    console.log("cpp_engine closed:", code);

    cppProcess = null;

    if (win && !win.isDestroyed()) {
      win.webContents.send("cpp:closed", code);
    }
  });

  console.log("cpp_engine started with PID:", cppProcess.pid);

  return {
    ok: true,
    pid: cppProcess.pid,
    message: "cpp_engine started",
  };
});

ipcMain.handle("cpp:stop", async () => {
  if (!cppProcess) {
    return {
      ok: true,
      message: "cpp_engine is not running",
    };
  }

  cppProcess.kill("SIGTERM");
  cppProcess = null;

  return {
    ok: true,
    message: "cpp_engine stopped",
  };
});

app.whenReady().then(() => {
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on("before-quit", () => {
  if (cppProcess) {
    cppProcess.kill("SIGTERM");
    cppProcess = null;
  }
});

app.on("window-all-closed", () => {
  if (cppProcess) {
    cppProcess.kill("SIGTERM");
    cppProcess = null;
  }

  if (process.platform !== "darwin") {
    app.quit();
  }
});
import { contextBridge, ipcRenderer } from "electron";
import { existsSync, readFileSync } from "fs";

const SHM_PATH = "/tmp/realtime_encode_camera_frame.shm";

// Very likely 40, not 48, based on your C++ header fields:
// uint64 seq + uint64 timestamp + 5 ints = 16 + 20 + padding = 40
const HEADER_SIZE = 40;

contextBridge.exposeInMainWorld("api", {
  startCppEngine: () => ipcRenderer.invoke("cpp:start"),
  stopCppEngine: () => ipcRenderer.invoke("cpp:stop"),

  getLatestFrame: () => {
    if (!existsSync(SHM_PATH)) return null;

    const buffer = readFileSync(SHM_PATH);

    if (buffer.length < HEADER_SIZE) return null;

    const seq1 = Number(buffer.readBigUInt64LE(0));

    if (seq1 % 2 === 1) return null;

    const timestampNs = Number(buffer.readBigUInt64LE(8));
    const width = buffer.readInt32LE(16);
    const height = buffer.readInt32LE(20);
    const channels = buffer.readInt32LE(24);
    const stride = buffer.readInt32LE(28);
    const frameBytes = buffer.readInt32LE(32);

    if (!width || !height || channels !== 4 || frameBytes <= 0) {
      return null;
    }

    const pixelsStart = HEADER_SIZE;
    const pixelsEnd = pixelsStart + frameBytes;

    if (buffer.length < pixelsEnd) return null;

    const seq2 = Number(buffer.readBigUInt64LE(0));

    if (seq1 !== seq2 || seq2 % 2 === 1) return null;

    return {
      seq: seq2,
      timestampNs,
      width,
      height,
      channels,
      stride,
      frameBytes,
      pixels: new Uint8ClampedArray(buffer.slice(pixelsStart, pixelsEnd)),
    };
  },
});
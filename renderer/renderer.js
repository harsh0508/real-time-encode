const video = document.getElementById("video");
const startBtn = document.getElementById("startBtn");
const stopBtn = document.getElementById("stopBtn");
const uploadBtn = document.getElementById("upload-stream");
const fileInput = document.getElementById("fileInput");
const statusText = document.getElementById("status");
const streamType = document.getElementById("streamType");
const emptyState = document.getElementById("emptyState");

let currentStream = null;
let currentSource = "none";
let frameTimer = null;
let uploadedObjectUrl = null;

const canvas = document.createElement("canvas");
const ctx = canvas.getContext("2d", { willReadFrequently: true });

const FRAME_SEND_INTERVAL_MS = 33;
// 33ms = around 30 FPS.
// Increase to 66ms for around 15 FPS if C++ engine becomes slow.

function setStatus(message) {
  statusText.textContent = `Status: ${message}`;
}

function setSourceLabel(source) {
  streamType.textContent = `Source: ${source}`;
}

function showPreviewState(active) {
  emptyState.style.display = active ? "none" : "block";
  stopBtn.disabled = !active;
}

async function notifyCppEngineStreamStarted(source) {
  try {
    if (window.api?.startCppStream) {
      await window.api.startCppStream({ source });
    } else if (window.electronAPI?.startCppStream) {
      await window.electronAPI.startCppStream({ source });
    }
  } catch (error) {
    console.error("Could not notify C++ engine start:", error);
  }
}

async function notifyCppEngineStreamStopped() {
  try {
    if (window.api?.stopCppStream) {
      await window.api.stopCppStream();
    } else if (window.electronAPI?.stopCppStream) {
      await window.electronAPI.stopCppStream();
    }
  } catch (error) {
    console.error("Could not notify C++ engine stop:", error);
  }
}

async function sendFrameToCppEngine(payload) {
  try {
    if (window.api?.sendVideoFrame) {
      await window.api.sendVideoFrame(payload);
    } else if (window.electronAPI?.sendVideoFrame) {
      await window.electronAPI.sendVideoFrame(payload);
    } else {
      // Fallback while backend bridge is not connected.
      // Remove this once preload/main IPC is wired.
      console.log("Frame ready for C++ engine:", {
        width: payload.width,
        height: payload.height,
        source: payload.source,
        bytes: payload.frame.byteLength,
      });
    }
  } catch (error) {
    console.error("Failed to send frame to C++ engine:", error);
  }
}

function startFramePump() {
  stopFramePump();

  frameTimer = setInterval(() => {
    if (!video.videoWidth || !video.videoHeight) return;
    if (video.paused || video.ended) return;

    canvas.width = video.videoWidth;
    canvas.height = video.videoHeight;

    ctx.drawImage(video, 0, 0, canvas.width, canvas.height);

    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);

    sendFrameToCppEngine({
      width: canvas.width,
      height: canvas.height,
      source: currentSource,
      timestamp: performance.now(),

      // RGBA raw frame buffer.
      // C++ side can convert RGBA -> BGR/YUV before encoding.
      frame: imageData.data.buffer,
    });
  }, FRAME_SEND_INTERVAL_MS);
}

function stopFramePump() {
  if (frameTimer) {
    clearInterval(frameTimer);
    frameTimer = null;
  }
}

async function startCamera() {
  try {
    stopCurrentStream();

    currentStream = await navigator.mediaDevices.getUserMedia({
      video: {
        width: { ideal: 1280 },
        height: { ideal: 720 },
        frameRate: { ideal: 30 },
      },
      audio: false,
    });

    currentSource = "camera";

    video.srcObject = currentStream;
    video.muted = true;
    video.controls = false;

    await video.play();

    showPreviewState(true);
    setSourceLabel("Camera");
    setStatus("Camera started. Sending frames to C++ engine.");

    await notifyCppEngineStreamStarted("camera");
    startFramePump();
  } catch (error) {
    console.error(error);
    setStatus("Could not start camera. Check camera permission.");
    showPreviewState(false);
  }
}

function uploadStream() {
  fileInput.click();
}

async function handleUploadedVideo(event) {
  const file = event.target.files?.[0];
  if (!file) return;

  stopCurrentStream();

  uploadedObjectUrl = URL.createObjectURL(file);
  currentSource = "uploaded-video";

  video.srcObject = null;
  video.src = uploadedObjectUrl;
  video.muted = true;
  video.controls = false;
  video.loop = false;

  try {
    await video.play();

    showPreviewState(true);
    setSourceLabel(`Uploaded Video`);
    setStatus(`Playing "${file.name}". Sending frames to C++ engine.`);

    await notifyCppEngineStreamStarted("uploaded-video");
    startFramePump();
  } catch (error) {
    console.error(error);
    setStatus("Could not play uploaded video.");
    showPreviewState(false);
  }
}

function stopCurrentStream() {
  stopFramePump();

  if (currentStream) {
    currentStream.getTracks().forEach((track) => track.stop());
    currentStream = null;
  }

  if (uploadedObjectUrl) {
    URL.revokeObjectURL(uploadedObjectUrl);
    uploadedObjectUrl = null;
  }

  video.pause();
  video.srcObject = null;
  video.removeAttribute("src");
  video.load();

  currentSource = "none";

  showPreviewState(false);
  setSourceLabel("None");
  setStatus("Stream stopped.");

  notifyCppEngineStreamStopped();
}

video.addEventListener("ended", () => {
  if (currentSource === "uploaded-video") {
    stopCurrentStream();
  }
});

startBtn.addEventListener("click", startCamera);
stopBtn.addEventListener("click", stopCurrentStream);
uploadBtn.addEventListener("click", uploadStream);
fileInput.addEventListener("change", handleUploadedVideo);
const startBtn = document.getElementById("startBtn");
const stopBtn = document.getElementById("stopBtn");
const statusText = document.getElementById("status");
const emptyState = document.getElementById("emptyState");
const streamType = document.getElementById("streamType");

const canvas = document.getElementById("cppCanvas");
const ctx = canvas.getContext("2d", {
  alpha: false,
  desynchronized: true,
});

let running = false;
let animationId = null;
let lastSeq = null;
let frameCount = 0;
let lastFpsTime = performance.now();

function setStatus(message) {
  if (statusText) {
    statusText.textContent = `Status: ${message}`;
  }
}

function setSourceLabel(source) {
  if (streamType) {
    streamType.textContent = `Source: ${source}`;
  }
}

function showPreviewState(active) {
  if (emptyState) {
    emptyState.style.display = active ? "none" : "block";
  }

  if (stopBtn) {
    stopBtn.disabled = !active;
  }

  if (startBtn) {
    startBtn.disabled = active;
  }
}

async function startCamera() {
  try {
    setStatus("Starting C++ engine...");

    const result = await window.api.startCppEngine();

    if (!result || !result.ok) {
      setStatus(result?.message || "Failed to start C++ engine.");
      return;
    }

    running = true;
    lastSeq = null;
    frameCount = 0;
    lastFpsTime = performance.now();

    showPreviewState(true);
    setSourceLabel("C++ Camera Shared Memory");
    setStatus("C++ engine started. Waiting for shared memory frame...");

    drawLoop();
  } catch (error) {
    console.error(error);
    setStatus(`Start error: ${error.message}`);
  }
}

async function stopCamera() {
  try {
    running = false;

    if (animationId) {
      cancelAnimationFrame(animationId);
      animationId = null;
    }

    await window.api.stopCppEngine();

    if (ctx && canvas) {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
    }

    showPreviewState(false);
    setSourceLabel("None");
    setStatus("C++ engine stopped.");
  } catch (error) {
    console.error(error);
    setStatus(`Stop error: ${error.message}`);
  }
}

function drawLoop() {
  if (!running) return;

  try {
    const frame = window.api.getLatestFrame();

    if (frame && frame.pixels) {
      drawFrame(frame);
      updateFps(frame);
    }
  } catch (error) {
    console.error("Frame read error:", error);
  }

  animationId = requestAnimationFrame(drawLoop);
}

function drawFrame(frame) {
  const { width, height, pixels } = frame;

  if (!width || !height || !pixels) return;

  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;

    canvas.style.width = "100%";
    canvas.style.height = "100%";
  }

  const imageData = new ImageData(
    pixels,
    width,
    height
  );

  ctx.putImageData(imageData, 0, 0);
}

function updateFps(frame) {
  frameCount++;

  const now = performance.now();
  const elapsed = now - lastFpsTime;

  if (elapsed >= 1000) {
    const fps = Math.round((frameCount * 1000) / elapsed);

    setStatus(
      `Reading latest shared-memory frame | Display FPS: ${fps}`
    );

    frameCount = 0;
    lastFpsTime = now;
  }
}

if (startBtn) {
  startBtn.addEventListener("click", startCamera);
}

if (stopBtn) {
  stopBtn.addEventListener("click", stopCamera);
}

showPreviewState(false);
setSourceLabel("None");
setStatus("Idle");
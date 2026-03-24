// renderer/renderer.js
const video = document.getElementById("video");
async function startCamera() {
    const stream = await navigator.mediaDevices.getUserMedia({ video: true });
    video.srcObject = stream;
    captureFramesGPU();
}
function captureFramesGPU() {
    const canvas = document.createElement("canvas");
    const gl = canvas.getContext("webgl2");
    // create a texture for GPU
    if (gl != null) {
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        setInterval(() => {
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);
            // send GPU handle to main process
            window.api.sendFrameHandle(texture); // electron main receives GPU handle
        }, 33); // ~30 FPS
    }
}
startCamera();
export {};

const startBtn = document.getElementById("startBtn")
const stopBtn = document.getElementById("stopBtn") 
const video = document.getElementById("video") 

let stream= null

async function startPreview() {
  try {
    stream = await navigator.mediaDevices.getUserMedia({
      video: true,
      audio: false,
    })

    video.srcObject = stream
    await video.play()

    console.log("Camera started")
  } catch (err) {
    console.error("Could not start camera:", err)
  }
}

function stopPreview() {
  if (stream) {
    stream.getTracks().forEach((track) => track.stop())
    stream = null
  }

  video.srcObject = null
  console.log("Camera stopped")
}

window.addEventListener("DOMContentLoaded", () => {
  startBtn?.addEventListener("click", startPreview)
  stopBtn?.addEventListener("click", stopPreview)
})
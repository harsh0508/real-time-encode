export {}

declare global {
  interface Window {
    api: {
      sendFrame: (data: ArrayBuffer) => void
      sendFrameHandle: (handle: any) => void
    }
  }
}
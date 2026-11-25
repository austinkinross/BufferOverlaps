# BufferOverlaps

Quick tool that tells you which placed buffers in a PIX capture overlap a given buffer. Uses the PIX API, so you need PIX installed (I used 2611.19, change the path at the top of the .cpp if yours is different).

```
.\build.ps1
.\BufferOverlaps.exe mycapture.wpix <buffer api object id>
```

It prints the API object IDs of the overlapping buffers. Only looks at placed buffers, not textures.

Add `--writes` on the end and it replays the capture and prints every event that changes the buffer's contents instead (any buffer, not just placed ones). Slow on big captures since it reads the buffer back after every event.

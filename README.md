Run rtmp receive server then

ffplay -fflags nobuffer -flags low_delay -framedrop -probesize 32 -analyzeduration 0 rtmp://localhost:1935/live/stream_480
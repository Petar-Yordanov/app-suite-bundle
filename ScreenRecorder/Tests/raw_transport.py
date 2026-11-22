"""Integration experiment for the Unix raw-video/raw-PCM bridge (Linux/macOS)."""
import json
import pathlib
import socket
import subprocess
import tempfile
import threading
import time


def serve(path, payload, period, done):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
        server.bind(str(path))
        server.listen(1)
        server.settimeout(8)
        try:
            sock, _ = server.accept()
            with sock:
                sock.settimeout(2)
                began = time.perf_counter()
                next_chunk = 0
                while not done.is_set():
                    sock.sendall(payload)
                    next_chunk += 1
                    done.wait(max(0, began + next_chunk*period - time.perf_counter()))
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass


def main():
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM):
            pass
    except (AttributeError, OSError) as error:
        print(f"SKIP: Unix-domain sockets are unavailable: {error}")
        return
    with tempfile.TemporaryDirectory(prefix="rec-transport-", dir="/tmp") as directory:
        root = pathlib.Path(directory)
        video, audio = root / "video", root / "audio"
        done = threading.Event()
        threads = [
            threading.Thread(target=serve,args=(video, b"\x20\x40\x80\xff"*(64*64), 1/25, done)),
            threading.Thread(target=serve,args=(audio, bytes(480*8), .01, done)),
        ]
        for thread in threads:
            thread.start()
        try:
            deadline = time.monotonic() + 2
            while (not video.exists() or not audio.exists()) and time.monotonic()<deadline:
                time.sleep(.01)
            args = ["ffmpeg", "-hide_banner","-loglevel","error","-y",
                    "-progress","pipe:1",
                    "-thread_queue_size","4","-use_wallclock_as_timestamps","1",
                    "-probesize","32","-analyzeduration","0","-f","rawvideo","-pixel_format","bgra",
                    "-video_size","64x64","-framerate","25","-i","unix:"+str(video),
                    "-thread_queue_size","32","-use_wallclock_as_timestamps","1","-isync","0",
                    "-probesize","32","-analyzeduration","0","-f","f32le","-ar","48000","-ac","2","-i","unix:"+str(audio),
                    "-map","0:v:0","-map","1:a:0","-af","aresample=48000:async=1000:first_pts=0",
                    "-c:a","aac","-c:v","libx264","-preset","ultrafast","-pix_fmt","yuv420p",
                    "-bf","0","-r","25","-fps_mode","cfr","-t","1.5",str(root/"capture.mp4")]
            subprocess.run(args, check=True, capture_output=True, timeout=20)
            metadata=json.loads(subprocess.check_output(["ffprobe","-v","error","-show_streams","-of","json",str(root/"capture.mp4")]))
            v,a=metadata["streams"]
            assert v["codec_name"]=="h264" and a["codec_name"]=="aac"
            assert abs(float(v["duration"])-float(a["duration"]))<.2
            assert int(v["nb_frames"])>=35
            print("PASS native raw-video/raw-PCM socket transport, CFR output and synchronized durations")
        finally:
            done.set()
            for thread in threads:
                thread.join(10)


if __name__ == "__main__":
    main()

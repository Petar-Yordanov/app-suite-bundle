"""Real FFmpeg integration experiment; no display or Qt required.

Matches the software encoding, two-audio mixing and concat options in Ffmpeg.cpp.
The C++ integration test separately exercises the actual argument builder.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile


def run(args, cwd):
    return subprocess.check_output(args, cwd=cwd, stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="screenrecorder-test-") as temp:
        p = pathlib.Path(temp)
        for i in range(2):
            out = f"part-{i}.mp4"
            run([args.ffmpeg, "-hide_banner", "-loglevel", "warning", "-y",
                 "-filter_threads", "2", "-filter_complex_threads", "2",
                 "-progress", "pipe:1", "-stats_period", "0.25", "-nostats",
                 "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=24",
                 "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
                 "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=48000",
                 "-map", "0:v:0", "-filter_complex",
                 "[1:a:0]aresample=48000:async=1000:first_pts=0,aformat=channel_layouts=stereo[a];"
                 "[2:a:0]aresample=48000:async=1000:first_pts=0,aformat=channel_layouts=stereo[b];"
                 "[a][b]amix=inputs=2:duration=longest:normalize=1[mix]",
                 "-map", "[mix]", "-c:a", "aac", "-ar", "48000", "-ac", "2", "-b:a", "192k",
                 "-vf", "scale=320:240:flags=fast_bilinear,setsar=1", "-c:v", "libx264",
                 "-b:v", "1500k", "-maxrate", "3000k", "-bufsize", "3000k", "-g", "48",
                 "-bf", "0", "-r", "24", "-fps_mode", "cfr", "-pix_fmt", "yuv420p",
                 "-preset", "ultrafast", "-threads", "4", "-video_track_timescale", "90000",
                 "-f", "mp4", "-t", "1", out], p)
        (p / "segments.txt").write_text("file 'part-0.mp4'\nfile 'part-1.mp4'\n", encoding="ascii")
        run([args.ffmpeg, "-hide_banner", "-loglevel", "warning", "-y", "-f", "concat", "-safe",
             "1", "-i", "segments.txt", "-map", "0", "-c", "copy", "-movflags", "+faststart", "joined.mp4"], p)
        run([args.ffmpeg, "-v", "error", "-i", "joined.mp4", "-map", "0:v:0", "-map", "0:a:0",
             "-f", "null", "-"], p)
        metadata = json.loads(run([args.ffprobe, "-v", "error", "-show_streams", "-show_format", "-of",
                                   "json", "joined.mp4"], p))
        v, a = metadata["streams"]
        assert v["codec_name"] == "h264" and a["codec_name"] == "aac"
        assert int(v["width"]) == 320 and int(v["height"]) == 240
        assert a["sample_rate"] == "48000" and a["channels"] == 2
        assert 1.9 < float(metadata["format"]["duration"]) < 2.2
        assert abs(float(v["duration"]) - float(a["duration"])) < .15
        print("PASS H.264, mixed AAC, pause-segment concat, decode, duration and A/V duration agreement")


if __name__ == "__main__":
    main()

"""Check decoded synthetic OBS canary frames, not just RTMP connection logs."""

import argparse
import json
import subprocess


EXPECTED = {
    "dsk-horizontal": ((1920, 1080), (255, 124, 45)),
    "dsk-horizontal-canvas": ((1920, 1080), (45, 140, 255)),
    "dsk-vertical": ((1080, 1920), (255, 140, 45)),
    "hidden-horizontal": ((1920, 1080), (0, 0, 0)),
}


def check_media(path, group, time_seconds=1):
    dimensions, color = EXPECTED[group]
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=width,height", "-of", "json", path],
        check=True, capture_output=True, text=True, timeout=20,
    )
    stream = json.loads(probe.stdout)["streams"][0]
    actual_dimensions = (stream["width"], stream["height"])
    if actual_dimensions != dimensions:
        raise AssertionError(f"{group}: dimensions {actual_dimensions}, expected {dimensions}")
    frames = subprocess.run(
        ["ffmpeg", "-v", "error", "-threads", "1", "-ss", str(time_seconds), "-i", path,
         "-frames:v", "1", "-vf", "scale=16:16:flags=area", "-pix_fmt", "rgb24",
         "-f", "rawvideo", "-"],
        check=True, capture_output=True, timeout=20,
    ).stdout
    if len(frames) != 16 * 16 * 3:
        raise AssertionError(f"{group}: a decoded RGB frame was not returned")
    average = tuple(round(sum(frames[channel::3]) / 256) for channel in range(3))
    max_error = max(abs(value - color[index % 3]) for index, value in enumerate(frames))
    if max_error > 8:
        raise AssertionError(f"{group}: decoded RGB {average}, expected {color}; max error {max_error}")
    return {"group": group, "time_seconds": time_seconds, "dimensions": list(actual_dimensions),
            "rgb": list(average), "passed": True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--expect", nargs=2, action="append", required=True, metavar=("GROUP", "FILE"))
    parser.add_argument("--time-seconds", type=float, default=1)
    args = parser.parse_args()
    if not 0 <= args.time_seconds < float("inf"):
        parser.error("--time-seconds must be finite and non-negative")
    results = []
    for group, path in args.expect:
        try:
            results.append(check_media(path, group, args.time_seconds))
        except (AssertionError, subprocess.SubprocessError, KeyError, IndexError) as error:
            results.append({"group": group, "passed": False, "error": str(error)})
    print(json.dumps(results, indent=2))
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())

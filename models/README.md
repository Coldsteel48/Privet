# Face detection / embedding models

pam_facial uses two small ONNX models from the [OpenCV
Zoo](https://github.com/opencv/opencv_zoo) (Apache-2.0), loaded via
OpenCV's `objdetect` module (`cv::FaceDetectorYN`, `cv::FaceRecognizerSF`)
— see `core/face/FaceDetector.*` and `core/face/FaceEmbedder.*`.

Model files are **not committed to this repository** (`.onnx` is
gitignored) — run `./download-models.sh` to fetch them into this
directory.

| Model | File | SHA-256 | Size |
|---|---|---|---|
| Face detection (YuNet) | `face_detection_yunet_2023mar.onnx` | `8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4` | ~227 KB |
| Face recognition (SFace) | `face_recognition_sface_2021dec.onnx` | `0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79` | ~37 MB |

These checksums were pinned directly against the OpenCV Zoo's Git LFS
object hashes at the time this was written — `download-models.sh`
verifies them after every download and refuses to leave a
corrupted/mismatched file in place.

## Note on IR accuracy (open risk)

Both models are trained primarily on visible-spectrum RGB face images.
Their accuracy on genuine single-channel IR frames (once
`V4L2Camera`'s `GREY`/`Y16` paths are implemented — see the project plan)
is **not yet verified** and should be checked against real hardware
before relying on it. If accuracy turns out to be poor on IR input, `dlib`
(available via `pacman`, not AUR) is the documented fallback — see the
project plan's core-library design notes.

## Why not a direct `raw.githubusercontent.com` link

`opencv_zoo` stores its `.onnx` files via Git LFS. A plain
`raw.githubusercontent.com` URL for one of these files returns a small
LFS *pointer* text file (~130 bytes), not the actual model — silently
breaking model loading later with a confusing "failed to load model"
error. `download-models.sh` fetches from `media.githubusercontent.com`
instead, which serves the real LFS object content.

# MAO showcase

Stills and a 57 s product film of MAO_MAIN A0 in a **concept enclosure** (made up for the film, not the final
enclosure). Everything is rendered locally: KiCad's raytracer for the 3D, numpy/PIL for the compositing, scipy for the
soundtrack, AVFoundation for the encode. Nothing is downloaded and nothing carries a third-party licence.

| Step | Command | Output |
|---|---|---|
| Enclosure models | `python3 enclosure.py` | `models/*.wrl` (VRML, KiCad units) |
| Scene | KiCad python `scene.py` | `build/scene.kicad_pcb`: the board plus the enclosure as one footprint |
| Film 3D | `python3 shots.py film 3` | `build/frames/<segment>/NNNN.png`, about 5 s a frame |
| Stills 3D | `python3 shots.py stills 3` | `build/stills/*.png`, 4K, transparent |
| Callout anchors | `python3 locate.py render`, then `routerenv locate.py measure` | `build/labels.json` |
| Soundtrack | `routerenv audio.py` | `build/soundtrack.wav` |
| Film | `routerenv compose.py all 4` | `build/out/NNNN.jpg` |
| Encode | `swiftc -O encode.swift -o build/encode`, then `build/encode build/out 30 out/MAO-showcase.mp4 build/soundtrack.wav` | H.264 + AAC |
| Plates | `routerenv plates.py` | `out/stills/*.jpg`, `out/renders/*.png` |

`routerenv` is any Python with numpy, scipy and Pillow. Rendering resumes: frames already on disk are skipped, so delete
a segment's folder after changing its shot.

Notes
- KiCad renders 16 px smaller than asked (1904 x 1064 for 1920 x 1080); the compositor centres them.
- `${KIPRJMOD}` models are rewritten to absolute paths per frame, since each frame renders from a temp folder.
- The speaker is the board's own B.Cu model; the enclosure only adds base, cell, LRA, display, window, bezel, ring.
- Timing lives in three places that must agree: `shots.py` (segment lengths), `compose.py` (scenes) and `audio.py`
  (events).

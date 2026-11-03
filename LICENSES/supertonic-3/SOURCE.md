# Pinned Supertonic 3 model

`LICENSE` in this directory is the verbatim license file from the Supertonic 3
model repository:

- Upstream model: <https://huggingface.co/supertone-oss-archive/supertonic-3>
- Revision: `aafc6e32416a594460b32413efc49d7fe4ce6d46`
- License: BigScience Open RAIL-M (see `LICENSE`)
- Upstream inference code: <https://github.com/supertone-oss-archive/supertonic>
  (MIT); the plugin ports that C++ path to Qt and ONNX Runtime.

The Flatpak manifest downloads every model file from that revision and pins
each one by SHA-256.  The installed layout is:

```
supertonic/onnx/{duration_predictor,text_encoder,vector_estimator,vocoder}.onnx
supertonic/onnx/tts.json
supertonic/onnx/unicode_indexer.json
supertonic/voice_styles/{F1..F5,M1..M5}.json
supertonic/LICENSE
```

The model is bundled for offline, in-process inference.  The plugin sends no
text or audio to a network service.

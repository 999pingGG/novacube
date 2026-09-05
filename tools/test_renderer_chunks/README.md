# CPU chunk checks (AI-assisted)

These checks run actual renderer registration, update, culling, packing, and upload code with fake Vulkan/VMA
endpoints and deterministic mesher output. They do not open a window or exercise a GPU driver.

From the repository root, with SDL3 and the Vulkan SDK installed:

```sh
cmake -S tools/test_renderer_chunks -B build/chunk-checks
cmake --build build/chunk-checks --config Release
ctest --test-dir build/chunk-checks -C Release --output-on-failure
```

Coverage includes empty/opaque/transparent/mixed chunks, multiple pages, frustum rejection, stable-ID reuse,
swap-removal lookup repair, metadata-buffer growth, packed uploads, replacement failures, deferred allocation
retirement, and matching multi-draw/single-record submissions. Quads remain eight bytes.

For gameplay verification, start the game normally and check streaming, boundary edits, lighting, water, and
frustum edges. Set `NC_FORCE_SINGLE_DRAW_INDIRECT=1` before launching to force the fallback on a multi-draw device.
The normal path is restored by removing the environment variable. Mobile driver validation still requires playing
on the affected Adreno and PowerVR devices.

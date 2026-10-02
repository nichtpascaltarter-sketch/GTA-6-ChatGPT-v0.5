# Audio output and device-reopen audit

The original unsigned eight-bit PCM conversion encoded digital silence as 127
instead of its required midpoint, 128. It also bypassed extensible valid-bit
alignment for eight-bit containers. The output packer now quantizes at the valid
precision, aligns the result inside the container, and handles signed PCM and
unsigned eight-bit PCM separately. Float output remains bit-identical for valid
samples. NaN becomes silence; infinities and finite over-range values are
clipped before encoding. Unused surround channels receive exact digital silence.

`audio_output_format.h` contains this portable, allocation-free conversion.
WASAPI retains ownership of the destination buffer and still validates channel
count, sample rate, block alignment, subtype and precision before starting.
Malformed precision is rejected rather than shifted with an invalid count.

The nonblocking handoff originally could treat `AudioState{}` as a real first
snapshot and play its default radio station before any game publication.
`AudioMailbox` now returns no snapshot until `publish` occurs. Its unpublished or
contended read leaves the worker's explicitly silent fallback unchanged. A
mutex protects both the complete payload and its publication flag; the worker
only uses `try_to_lock`.

On reopening an endpoint, a newly constructed synth previously treated a cached
nonzero muzzle flash as a new gunshot. `Synth::prime` consumes that first real
snapshot's shot indicator as a baseline. Later rises still fire normally, and
ordinary `Synth::update` retains its original behavior. Each device stream
primes exactly once, after its first successful mailbox read.

Strict and ASan/UBSan/leak checks pass for `tests/audio_output_tests.cpp`:

```sh
g++ -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror -pthread tests/audio_output_tests.cpp -o /tmp/audio_output_tests
/tmp/audio_output_tests
g++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer -fsanitize=address,undefined -pthread tests/audio_output_tests.cpp -o /tmp/audio_output_tests_sanitized
ASAN_OPTIONS=detect_leaks=1 /tmp/audio_output_tests_sanitized
```

Checks include exact PCM bytes at negative/positive full scale, zero and half
scale; all 80 supported container/valid-precision combinations; 24-bit precision
inside a 32-bit container; unaligned float destinations; mono cancellation;
stereo float identity; surround silence; output guards; malformed formats;
nonfinite inputs; historical versus fresh gunshots; and 30,000 concurrent
mailbox publications with coherent source arrays and final-state delivery.
Independent review additionally passed 400,000 PCM cases under sanitizers.

The ordinary synthesis fingerprint remains identical to the verified parent:
2,319,840 float samples / 9,279,360 bytes, SHA256
`0af02e039470d8a75a07d2828e362df41aef4164109ddc7cc2c1afb4739a485d`.

Read-through found no additional defect in stop-event signaling, worker joining,
COM-object release ordering, or retrying an invalidated endpoint. Default-device
changes that leave the old endpoint active are not currently followed through
device notifications. Native WASAPI startup, removal/recovery, output routing
and listening remain unverified by these portable checks.

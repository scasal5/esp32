# Measurement window

Builds 1, 2 and 3 start memory sampling at least ten seconds after their
synchronous application initialization finishes. The first sample is preceded
by a `measurement_ready` event recording `init_complete_us`, `time_us` and
`settle_us=10000000`. Build 1 starts its telemetry task after product services
have been started; builds 2/3 begin the interval after host/guest initialization.
This is a reproducible settling interval, not proof that asynchronous work has
stopped or that STA has connected. Compare STA samples only with connection
evidence. Heap minimum remains the lifetime minimum, including initialization.

All builds emit `memory` records and separate `wifi` records with `connected`.
The capture tool preserves raw bytes, including console noise. The report reader
accepts complete JSON records, including those preceded by the product's
`ws183>` prompt, and discards malformed/interleaved lines. It does not reconstruct
damaged samples. Older captures without the readiness marker cannot establish
this measurement-window convention retroactively.

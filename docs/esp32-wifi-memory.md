# ESP32 Wi-Fi control replies (build 26)

After configuring 60 LEDs and joining Wi-Fi on the Gledopto ESP32-D0WD-V3, build 25 emitted `4c58503100000000`: the LXP1 marker followed by a zero-byte payload. A diagnostic build confirmed that allocating the response object failed. Setup rejected this invalid frame and its serial receive loop then stopped permanently.

Ordinary requests no longer embed a gradient, and responses no longer embed a gradient or a second complete state. Nanopb allocates incoming gradients only when present. Gradient operations own temporary decoded records, while state replies borrow the current state under the existing mutex through encoding. The Protobuf wire format is unchanged. On the host, request storage falls from 4,144 to 1,800 bytes; response storage falls from 9,112 to 2,520 bytes. The UART output workspace remains bounded as in build 25.

Allocation failures now use a small, allocation-free error encoder that preserves the request ID and returns a valid Protobuf error, including when the UART output buffer itself cannot be allocated. Setup keeps receiving after a malformed frame, without automatically replaying commands.

Hardware verification used the saved Wi-Fi credentials and 60-LED configuration: 70 successful USB responses across three restarts, retained identity/settings, Wi-Fi association, and scanning while connected. Minimum reported free byte-addressable internal memory during these requests was 6,536 bytes. The application partition alone was replaced. This does not qualify every large animation operation or confirm successful cloud TLS announcements.

Native sanitizer tests cover borrowed state and full-size gradient round trips, decoding cleanup, and emergency responses with zero, ordinary, and maximum request IDs. ESP32 stack checks pass at 1,856/4,096 bytes for the control path. Setup browser regressions cover valid error replies and recovery after a zero-length frame.

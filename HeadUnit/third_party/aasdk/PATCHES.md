# AASDK Windows port

Source: https://github.com/opencardev/aasdk
Revision: `9bf6adf933665dee26532201719fac14a047ccf1`.
Imported directories: include, src, protobuf. GPL-3.0-or-later, as stated in
upstream source headers. Original notices are retained. COPYING contains the GPL
v3 text (obtained from OpenAuto's LICENSE). This linked HeadUnit application and
the adapted session code must be distributed in accordance with that license.

Session orchestration reference/adaptation:
https://github.com/opencardev/openauto/tree/4cc739b813622739b09352655581072fc4d39280
(`AndroidAutoEntity`, video, input and sensor services; GPL-3.0-or-later).

Local modifications:

- Replace removed Boost.Asio io_service names with io_context, use free
  dispatch/post functions and strand.context().
- Rename LogLevel::ERROR to ERROR_LEVEL to avoid the Windows ERROR macro;
  use __FUNCSIG__ for MSVC logging. Remove an unused boost/algorithm include.
- Messenger.stop rejects pending receive promises, breaking session ownership
  cycles on disconnect/cancellation.
- Cryptor.decrypt uses the plaintext length returned by OpenSSL instead of
  assuming every TLS record has exactly 29 bytes of overhead.
- Version response validates its minimum length and reads big-endian version
  numbers plus signed status. SSLWrapper does not log normal WANT_READ/WANT_WRITE
  results as errors.
- Native MSBuild static-library target and protoc generation live outside the
  imported tree. Only framing, TLS and the control/video/input/sensor channels
  are compiled. Upstream Linux packaging/build scripts are not invoked.

The application owns libusb and the transport lifetime; upstream USB discovery
and its independent device selection are not used. JSON logging is disabled.
The stock upstream identity certificate/key are protocol credentials, not user
secrets. No host-wide TLS or Windows security settings are changed.

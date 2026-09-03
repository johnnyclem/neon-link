// Intentionally empty. When CONFIG_NEON_SYNC is off this component must
// not carry an nsync::Node in BSS (≈28 KB of peer-clock windows) — the
// Ableton Link image on the C3 cannot spare it. session_esp.cpp is the
// real glue and is compiled only for Neon Sync builds.

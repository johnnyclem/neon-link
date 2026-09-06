#include "neon/telemetry/audio_follow_csv.hpp"

#include <cstdio>

namespace neon {

AudioFollowTelemetrySample audio_follow_telemetry_sample(
    const FollowStatus& st, int64_t t_us, bool following) {
  AudioFollowTelemetrySample s;
  s.t_us = t_us;
  s.lock = st.lock;
  s.subdiv = st.subdiv;
  s.onset_hz_x10 = st.onset_hz_x10;
  s.est_mbpm = st.mbpm;
  s.pub_mbpm = st.published_mbpm;
  s.onsets = st.onsets;
  s.rejects = st.rejects;
  s.following = following ? 1 : 0;
  return s;
}

size_t audio_follow_telemetry_csv_header(char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap,
      "t_us,lock,subdiv,onset_hz_x10,est_mbpm,pub_mbpm,onsets,rejects,"
      "following");
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

size_t audio_follow_telemetry_csv_line(const AudioFollowTelemetrySample& s,
                                       char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap, "%lld,%u,%u,%u,%lu,%lu,%lu,%lu,%u",
      static_cast<long long>(s.t_us), static_cast<unsigned>(s.lock),
      static_cast<unsigned>(s.subdiv),
      static_cast<unsigned>(s.onset_hz_x10),
      static_cast<unsigned long>(s.est_mbpm),
      static_cast<unsigned long>(s.pub_mbpm),
      static_cast<unsigned long>(s.onsets),
      static_cast<unsigned long>(s.rejects),
      static_cast<unsigned>(s.following));
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

}  // namespace neon

#include "DeviceController.h"

#include "neon/client/backoff.hpp"
#include "neon/client/bind.hpp"
#include "neon/client/patch.hpp"

namespace neon::plugin {
namespace {

int64_t now_ms() {
  return juce::Time::getMillisecondCounterHiRes();
}

}  // namespace

DeviceController::DeviceController()
    : juce::Thread("neon.http"), client_(http_) {
  apply_bind("neon-link.local");
  Snapshot s;
  s.bind = bind_;
  s.reach = Reachability::Offline;
  snap_ = std::make_shared<const Snapshot>(std::move(s));
}

DeviceController::~DeviceController() { requestStop(); }

void DeviceController::requestStop() {
  signalThreadShouldExit();
  notify();
  http_.close();
  stopThread(1200);
}

void DeviceController::post(Cmd c) {
  {
    const std::lock_guard<std::mutex> g(mu_);
    queue_.push_back(std::move(c));
  }
  notify();
}

void DeviceController::bind(const std::string& host_or_ip) {
  Cmd c;
  c.kind = Kind::Bind;
  c.host = host_or_ip;
  post(std::move(c));
}

void DeviceController::transport(neon::client::TransportOp op) {
  Cmd c;
  c.kind = Kind::Transport;
  c.top = op;
  post(std::move(c));
}

void DeviceController::tempoOp(neon::client::TempoOp op, int delta) {
  Cmd c;
  c.kind = Kind::TempoOp;
  c.tempo = op;
  c.delta = delta;
  post(std::move(c));
}

void DeviceController::setTempo(double bpm) {
  Cmd c;
  c.kind = Kind::SetTempo;
  c.bpm = bpm;
  post(std::move(c));
}

void DeviceController::resync(neon::client::ResyncOp op) {
  Cmd c;
  c.kind = Kind::Resync;
  c.rop = op;
  post(std::move(c));
}

void DeviceController::preset(neon::client::PresetOp op, int slot) {
  Cmd c;
  c.kind = Kind::Preset;
  c.pop = op;
  c.slot = slot;
  post(std::move(c));
}

void DeviceController::saveConfig(const neon::Config& cfg) {
  Cmd c;
  c.kind = Kind::SaveConfig;
  c.cfg = cfg;
  post(std::move(c));
}

void DeviceController::scanWifi() {
  Cmd c;
  c.kind = Kind::Scan;
  post(std::move(c));
}

void DeviceController::refreshAudioChannels() {
  Cmd c;
  c.kind = Kind::RefreshAudio;
  post(std::move(c));
}

void DeviceController::reboot() {
  Cmd c;
  c.kind = Kind::Reboot;
  post(std::move(c));
}

void DeviceController::factoryReset() {
  Cmd c;
  c.kind = Kind::FactoryReset;
  post(std::move(c));
}

void DeviceController::setEditorOpen(bool open) {
  const std::lock_guard<std::mutex> g(mu_);
  editor_open_ = open;
}

std::shared_ptr<const Snapshot> DeviceController::snapshot() const {
  const std::lock_guard<std::mutex> g(mu_);
  return snap_;
}

Bind DeviceController::bind_state() const {
  const std::lock_guard<std::mutex> g(mu_);
  return bind_;
}

Snapshot DeviceController::base_snapshot() const {
  Snapshot s;
  s.bind = bind_;
  s.has_config = have_config_;
  s.config = config_;
  s.secrets = secrets_;
  s.config_seq = config_seq_;
  s.saving = saving_;
  s.last_save_ok = last_save_ok_;
  s.save_message = save_message_;
  s.save_seq = save_seq_;
  s.scanning = scanning_;
  s.scan_message = scan_message_;
  s.scan = scan_;
  s.audio_channels = audio_channels_;
  s.audio_refreshing = audio_refreshing_;
  s.persist_lazy = client_.persist_lazy();
  return s;
}

void DeviceController::publish(Snapshot s) {
  auto p = std::make_shared<const Snapshot>(std::move(s));
  const std::lock_guard<std::mutex> g(mu_);
  snap_ = std::move(p);
}

void DeviceController::apply_bind(const std::string& raw) {
  std::string t = raw;
  while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
  size_t i = 0;
  while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
  t = t.substr(i);
  if (t.empty()) t = "neon-link.local";

  bound_by_ip_ = neon::client::is_ipv4_literal(t);
  bind_.connect_host = bound_by_ip_ ? t : neon::client::mdns_host(t);
  bind_.device_name = bound_by_ip_ ? bind_.device_name
                                   : neon::client::dns_label(t);
  if (!bound_by_ip_) bind_.ip.clear();
  client_.setTarget(bind_.connect_host.c_str(), 80);
  http_.close();
  have_config_ = false;
  last_status_rev_ = 0;
}

void DeviceController::fill_status(Snapshot* s,
                                   const neon::client::Status& st) {
  s->status = st;
  s->reach = Reachability::Online;
  if (!st.device_name.empty()) {
    bind_.device_name = st.device_name;
    if (!bound_by_ip_) {
      bind_.connect_host = neon::client::mdns_host(st.device_name);
    }
  }
  if (!st.ip.empty()) bind_.ip = st.ip;
  // Talk to the IPv4 address once we have it. neon-link.local goes
  // through mDNS, and getaddrinfo on that name hangs the save path when
  // Link Audio has the radio too busy to answer.
  if (!bind_.ip.empty()) {
    client_.setTarget(bind_.ip.c_str(), 80);
  }
  s->bind = bind_;
  if (st.tempo_valid && st.peers == 0 && !st.setup_ap) {
    s->banner = "NO LINK — no other peers on this session.";
  }
}

bool DeviceController::fetch_config() {
  neon::client::ConfigSecrets sec;
  auto r = client_.getConfig(&sec);
  if (!r.ok) {
    return false;
  }
  config_ = r.value;
  secrets_ = sec;
  have_config_ = true;
  ++config_seq_;
  return true;
}

bool DeviceController::poll_once() {
  auto r = client_.getStatus();
  if (!r.ok) {
    return false;
  }
  const bool need_cfg =
      !have_config_ || (r.value.rev != 0 && r.value.rev != last_status_rev_);
  if (need_cfg) {
    (void)fetch_config();
  }
  last_status_rev_ = r.value.rev;

  Snapshot s;
  {
    const std::lock_guard<std::mutex> g(mu_);
    s = base_snapshot();
  }
  fill_status(&s, r.value);
  publish(std::move(s));
  return true;
}

void DeviceController::run() {
  neon::client::Backoff backoff;
  bool ever_online = false;
  int64_t last_poll = 0;
  int64_t last_resolve = 0;
  Reachability reach = Reachability::Connecting;

  {
    Snapshot s;
    s.bind = bind_;
    s.reach = Reachability::Connecting;
    publish(std::move(s));
  }

  while (!threadShouldExit()) {
    std::vector<Cmd> cmds;
    bool editor = false;
    {
      const std::lock_guard<std::mutex> g(mu_);
      cmds.swap(queue_);
      editor = editor_open_;
    }
    for (const auto& c : cmds) {
      if (threadShouldExit()) break;
      switch (c.kind) {
        case Kind::Bind:
          apply_bind(c.host);
          ever_online = false;
          backoff.reset();
          reach = Reachability::Connecting;
          last_poll = 0;
          break;
        case Kind::Transport:
          (void)client_.transport(c.top);
          last_poll = 0;
          break;
        case Kind::TempoOp:
          (void)client_.tempoOp(c.tempo, c.delta);
          last_poll = 0;
          break;
        case Kind::SetTempo:
          (void)client_.setTempo(c.bpm);
          last_poll = 0;
          break;
        case Kind::Resync:
          (void)client_.resync(c.rop);
          last_poll = 0;
          break;
        case Kind::Preset:
          (void)client_.preset(c.pop, c.slot);
          last_poll = 0;
          last_status_rev_ = 0;
          have_config_ = false;  // recall writes a new config
          break;
        case Kind::SaveConfig: {
          {
            const std::lock_guard<std::mutex> g(mu_);
            saving_ = true;
            save_message_.clear();
            Snapshot s = base_snapshot();
            s.reach = ever_online ? Reachability::Online : reach;
            publish(std::move(s));
          }
          const std::string body = neon::client::config_put_body(c.cfg);
          neon::client::JsonPatch patch;
          patch.mergeObject(body.c_str(), body.size());
          // Lazy apply + 2 s NVS debounce: the httpd handler must not sit
          // in flash while Link Audio is saturating core 0.
          const auto persist = client_.persist_lazy()
                                   ? neon::client::Persist::Lazy
                                   : neon::client::Persist::Now;
          auto put = client_.putConfig(patch, persist);
          neon::Config applied = c.cfg;
          neon::client::ConfigSecrets sec = secrets_;
          bool ok = put.ok;
          std::string msg;
          if (put.ok) {
            applied = put.value;
            for (int i = 0; i < neon::kWifiSlots; ++i) {
              if (c.cfg.wifi[i].pass[0] != '\0') sec.wifi_has_pass[i] = true;
            }
            if (c.cfg.ap_pass[0] != '\0') sec.ap_has_pass = true;
            msg = persist == neon::client::Persist::Lazy
                      ? "Saved (will persist)."
                      : "Saved.";
          } else if (put.timed_out) {
            // A follow-up GET against a silent module is how Saving…
            // lasted forever. Surface the timeout and stop.
            msg = put.error.empty() ? "Save timed out." : put.error;
          } else {
            auto fallback = client_.getConfig(&sec);
            if (fallback.ok) {
              applied = fallback.value;
              ok = true;
              msg = "Saved.";
            } else {
              msg = put.error.empty() ? "Could not save." : put.error;
            }
          }
          {
            const std::lock_guard<std::mutex> g(mu_);
            saving_ = false;
            last_save_ok_ = ok;
            save_message_ = msg;
            ++save_seq_;
            if (ok) {
              config_ = applied;
              secrets_ = sec;
              have_config_ = true;
              ++config_seq_;
            }
            Snapshot s = base_snapshot();
            s.reach = ever_online ? Reachability::Online : reach;
            publish(std::move(s));
          }
          last_poll = 0;
          break;
        }
        case Kind::Scan: {
          {
            const std::lock_guard<std::mutex> g(mu_);
            scanning_ = true;
            scan_message_ = "Scanning…";
            Snapshot s = base_snapshot();
            s.reach = ever_online ? Reachability::Online : reach;
            publish(std::move(s));
          }
          auto r = client_.scan();
          {
            const std::lock_guard<std::mutex> g(mu_);
            scanning_ = false;
            if (r.ok) {
              scan_ = std::move(r.value);
              scan_message_ = scan_.empty()
                                  ? "Nothing found. The radio is 2.4 GHz only."
                                  : std::to_string(scan_.size()) +
                                        " found — pick one to fill a slot.";
            } else {
              scan_.clear();
              scan_message_ = "Scan failed.";
            }
            Snapshot s = base_snapshot();
            s.reach = ever_online ? Reachability::Online : reach;
            publish(std::move(s));
          }
          break;
        }
        case Kind::RefreshAudio: {
          {
            const std::lock_guard<std::mutex> g(mu_);
            audio_refreshing_ = true;
            Snapshot s = base_snapshot();
            s.reach = ever_online ? Reachability::Online : reach;
            publish(std::move(s));
          }
          auto r = client_.audioChannels();
          {
            const std::lock_guard<std::mutex> g(mu_);
            audio_refreshing_ = false;
            if (r.ok) audio_channels_ = r.value;
            Snapshot s = base_snapshot();
            s.reach = ever_online ? Reachability::Online : reach;
            publish(std::move(s));
          }
          break;
        }
        case Kind::Reboot:
          (void)client_.reboot();
          ever_online = false;
          have_config_ = false;
          last_status_rev_ = 0;
          last_poll = 0;
          break;
        case Kind::FactoryReset:
          (void)client_.factoryReset();
          ever_online = false;
          have_config_ = false;
          last_status_rev_ = 0;
          last_poll = 0;
          break;
      }
    }

    const int64_t t = now_ms();
    const int interval = editor ? 500 : 1000;
    if (bind_.connect_host.empty()) {
      wait(100);
      continue;
    }

    if (t - last_resolve > 5000 && !bound_by_ip_) {
      last_resolve = t;
      client_.setTarget(bind_.connect_host.c_str(), 80);
    }

    if (t - last_poll >= interval) {
      last_poll = t;
      if (poll_once()) {
        ever_online = true;
        backoff.reset();
        reach = Reachability::Online;
      } else {
        reach = ever_online ? Reachability::Reconnecting
                            : Reachability::Connecting;
        Snapshot s;
        {
          const std::lock_guard<std::mutex> g(mu_);
          s = base_snapshot();
        }
        s.bind = bind_;
        s.reach = reach;
        s.banner = reach == Reachability::Reconnecting
                       ? "Lost the module — retrying."
                       : "Looking for " + bind_.connect_host;
        publish(std::move(s));

        if (ever_online && !bind_.ip.empty() &&
            bind_.connect_host != bind_.ip) {
          client_.setTarget(bind_.ip.c_str(), 80);
        }
        const int delay = backoff.next_delay_ms();
        wait(static_cast<int>(delay));
        continue;
      }
    }
    wait(50);
  }
}

}  // namespace neon::plugin

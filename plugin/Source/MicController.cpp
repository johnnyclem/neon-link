#include "MicController.h"

#include "neon/client/backoff.hpp"
#include "neon/client/bind.hpp"

namespace neon::plugin {
namespace {

int64_t now_ms() {
  return juce::Time::getMillisecondCounterHiRes();
}

}  // namespace

MicController::MicController() : juce::Thread("neon.mic"), client_(http_) {
  apply_bind(neon::client::kMicDefaultHost);
  MicSnapshot s;
  s.bind = bind_;
  s.port = port_;
  s.reach = Reachability::Offline;
  s.status = neon::client::default_mic_status();
  s.config = neon::client::default_mic_config();
  snap_ = std::make_shared<const MicSnapshot>(std::move(s));
}

MicController::~MicController() { requestStop(); }

void MicController::requestStop() {
  signalThreadShouldExit();
  notify();
  http_.close();
  stopThread(1200);
}

void MicController::post(Cmd c) {
  {
    const std::lock_guard<std::mutex> g(mu_);
    queue_.push_back(std::move(c));
  }
  notify();
}

void MicController::bind(const std::string& host_or_ip) {
  Cmd c;
  c.kind = Kind::Bind;
  c.host = host_or_ip;
  post(std::move(c));
}

void MicController::hintIp(const std::string& ip) {
  if (!neon::client::is_ipv4_literal(ip)) {
    return;
  }
  const std::lock_guard<std::mutex> g(mu_);
  bind_.ip = ip;
  client_.setTarget(bind_.ip.c_str(), port_);
}

void MicController::patch(const neon::client::MicConfig& next) {
  Cmd c;
  c.kind = Kind::Patch;
  c.cfg = next;
  post(std::move(c));
}

void MicController::capture(neon::client::CaptureOp op) {
  Cmd c;
  c.kind = Kind::Capture;
  c.cop = op;
  post(std::move(c));
}

void MicController::refreshSources() {
  Cmd c;
  c.kind = Kind::RefreshSources;
  post(std::move(c));
}

void MicController::setEditorOpen(bool open) {
  const std::lock_guard<std::mutex> g(mu_);
  editor_open_ = open;
}

std::shared_ptr<const MicSnapshot> MicController::snapshot() const {
  const std::lock_guard<std::mutex> g(mu_);
  return snap_;
}

Bind MicController::bind_state() const {
  const std::lock_guard<std::mutex> g(mu_);
  return bind_;
}

int MicController::port() const {
  const std::lock_guard<std::mutex> g(mu_);
  return port_;
}

MicSnapshot MicController::base_snapshot() const {
  MicSnapshot s;
  s.bind = bind_;
  s.port = port_;
  s.has_config = have_config_;
  s.config = config_;
  s.config_seq = config_seq_;
  s.sources = sources_;
  s.capturing = capturing_;
  s.kind_mismatch = kind_mismatch_;
  s.banner = banner_;
  return s;
}

void MicController::publish(MicSnapshot s) {
  auto p = std::make_shared<const MicSnapshot>(std::move(s));
  const std::lock_guard<std::mutex> g(mu_);
  snap_ = std::move(p);
}

void MicController::apply_bind(const std::string& raw) {
  const auto parsed = neon::client::parse_mic_host(raw);
  bound_by_ip_ = neon::client::is_ipv4_literal(parsed.host);
  port_ = parsed.port;
  bind_.connect_host = bound_by_ip_ ? parsed.host : neon::client::mdns_host(parsed.host);
  if (!bound_by_ip_) {
    bind_.device_name = neon::client::dns_label(parsed.host);
  }
  if (bound_by_ip_) {
    bind_.ip = parsed.host;
  }
  const char* target =
      !bind_.ip.empty() ? bind_.ip.c_str() : bind_.connect_host.c_str();
  client_.setTarget(target, port_);
  http_.close();
  have_config_ = false;
  last_status_rev_ = 0;
  kind_mismatch_ = false;
  banner_.clear();
  sources_.clear();
}

void MicController::fill_status(MicSnapshot* s, const neon::client::MicStatus& st) {
  s->status = st;
  s->reach = Reachability::Online;
  if (!st.device_name.empty()) {
    bind_.device_name = st.device_name;
    if (!bound_by_ip_) {
      bind_.connect_host = neon::client::mdns_host(st.device_name);
    }
  }
  if (!st.ip.empty()) {
    bind_.ip = st.ip;
    client_.setTarget(bind_.ip.c_str(), port_);
  }
  s->bind = bind_;
  s->port = port_;
  if (st.presence == neon::client::MicPresence::Offline) {
    s->banner = "Phone app is not reporting.";
  } else if (!st.error.empty()) {
    s->banner = st.error;
  } else {
    s->banner.clear();
  }
  banner_ = s->banner;
}

bool MicController::fetch_config() {
  auto r = client_.getConfig();
  if (!r.ok) {
    return false;
  }
  config_ = r.value;
  have_config_ = true;
  ++config_seq_;
  return true;
}

bool MicController::fetch_sources() {
  auto r = client_.getSources();
  if (!r.ok) {
    return false;
  }
  sources_ = std::move(r.value);
  return true;
}

bool MicController::poll_once() {
  auto r = client_.getStatus();
  if (!r.ok) {
    if (r.error == neon::client::kKindMismatchModule) {
      kind_mismatch_ = true;
      banner_ = neon::client::kKindMismatchModule;
    }
    return false;
  }
  kind_mismatch_ = false;
  const bool need_cfg =
      !have_config_ || (r.value.rev != 0 && r.value.rev != last_status_rev_);
  if (need_cfg) {
    (void)fetch_config();
  }
  last_status_rev_ = r.value.rev;

  MicSnapshot s;
  {
    const std::lock_guard<std::mutex> g(mu_);
    s = base_snapshot();
  }
  fill_status(&s, r.value);
  publish(std::move(s));
  return true;
}

void MicController::run() {
  neon::client::Backoff backoff;
  bool ever_online = false;
  int64_t last_poll = 0;
  int64_t last_resolve = 0;
  Reachability reach = Reachability::Connecting;

  {
    MicSnapshot s;
    s.bind = bind_;
    s.port = port_;
    s.reach = Reachability::Connecting;
    s.status = neon::client::default_mic_status();
    s.config = neon::client::default_mic_config();
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
      if (threadShouldExit()) {
        break;
      }
      switch (c.kind) {
        case Kind::Bind:
          apply_bind(c.host);
          ever_online = false;
          backoff.reset();
          reach = Reachability::Connecting;
          last_poll = 0;
          break;
        case Kind::Patch: {
          if (kind_mismatch_) {
            break;
          }
          neon::client::MicConfig from;
          {
            const std::lock_guard<std::mutex> g(mu_);
            from = have_config_ ? config_ : neon::client::default_mic_config();
          }
          auto next = c.cfg;
          neon::client::sanitize_mic_config(&next);
          auto put = client_.putConfig(from, next);
          if (put.ok) {
            const std::lock_guard<std::mutex> g(mu_);
            config_ = put.value;
            have_config_ = true;
            ++config_seq_;
          }
          last_poll = 0;
          break;
        }
        case Kind::Capture: {
          if (kind_mismatch_) {
            break;
          }
          MicSnapshot pre;
          {
            const std::lock_guard<std::mutex> g(mu_);
            capturing_ = true;
            pre = base_snapshot();
          }
          pre.reach = ever_online ? Reachability::Online : reach;
          publish(std::move(pre));
          (void)client_.capture(c.cop);
          MicSnapshot post;
          {
            const std::lock_guard<std::mutex> g(mu_);
            capturing_ = false;
            post = base_snapshot();
          }
          post.reach = ever_online ? Reachability::Online : reach;
          publish(std::move(post));
          last_poll = 0;
          break;
        }
        case Kind::RefreshSources:
          if (!kind_mismatch_) {
            (void)fetch_sources();
          }
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
      client_.setTarget(bind_.connect_host.c_str(), port_);
    }

    if (t - last_poll >= interval) {
      last_poll = t;
      if (poll_once()) {
        ever_online = true;
        backoff.reset();
        reach = Reachability::Online;
        if (editor && sources_.empty()) {
          (void)fetch_sources();
        }
      } else {
        reach = ever_online && !kind_mismatch_ ? Reachability::Reconnecting
                                               : Reachability::Connecting;
        if (kind_mismatch_) {
          reach = Reachability::Offline;
        }
        MicSnapshot s;
        {
          const std::lock_guard<std::mutex> g(mu_);
          s = base_snapshot();
        }
        s.bind = bind_;
        s.port = port_;
        s.reach = reach;
        if (kind_mismatch_) {
          s.banner = neon::client::kKindMismatchModule;
          s.kind_mismatch = true;
        } else {
          const std::string target =
              bind_.ip.empty() ? bind_.connect_host : bind_.ip;
          s.banner = reach == Reachability::Reconnecting
                         ? "Lost the phone — retrying over HTTP (Link peers are not the VST)."
                         : "Looking for " + target +
                               " over HTTP — Bind is not Ableton Link.";
        }
        publish(std::move(s));

        if (ever_online && !bind_.ip.empty() && bind_.connect_host != bind_.ip) {
          client_.setTarget(bind_.ip.c_str(), port_);
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

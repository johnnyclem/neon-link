#include "DeviceController.h"

#include "neon/client/backoff.hpp"
#include "neon/client/bind.hpp"

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
}

bool DeviceController::poll_once() {
  auto r = client_.getStatus();
  Snapshot s;
  s.bind = bind_;
  s.persist_lazy = client_.persist_lazy();
  if (!r.ok) {
    return false;
  }
  s.status = r.value;
  s.reach = Reachability::Online;
  if (!s.status.device_name.empty()) {
    bind_.device_name = s.status.device_name;
    if (!bound_by_ip_) {
      bind_.connect_host = neon::client::mdns_host(s.status.device_name);
    }
  }
  if (!s.status.ip.empty()) bind_.ip = s.status.ip;
  s.bind = bind_;
  if (s.status.tempo_valid && s.status.peers == 0 && !s.status.setup_ap) {
    s.banner = "NO LINK — no other peers on this session.";
  }
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

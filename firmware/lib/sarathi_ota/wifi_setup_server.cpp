#include "wifi_setup_server.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>

#include "captive_dns.h"
#include "fixed_string.h"
#include "wifi_form.h"
#include "wifi_setup_page.h"

namespace sarathi::ota {
namespace {

constexpr const char* kTag = "wifi_setup";
constexpr size_t kMaxNetworks = 16;
constexpr size_t kMaxFormBytes = 512;
constexpr int kApChannel = 1;
constexpr int kApMaxClients = 2;
constexpr int kDnsPort = 53;
constexpr int kDnsPacketsPerPoll = 4;
constexpr uint32_t kStatusSettleMs = 1000;  // ignore a stale failure status right after begin()
constexpr int kHttpStackBytes = 6144;
constexpr uint32_t kScanTimeoutMs = 6000;  // an active scan of all channels takes ~4 s

// Swapped between the web server task and Poll() (UI task) under `lock`.
struct Shared {
  std::array<core::FixedString<core::kMaxSsidBytes>, kMaxNetworks> networks{};
  size_t network_count = 0;
  bool submission = false;
  core::WifiCredentials submitted;
  core::WifiSetupPhase phase = core::WifiSetupPhase::kIdle;
  bool last_join_failed = false;
  core::FixedString<core::kMaxSsidBytes> ssid;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
SemaphoreHandle_t lock = nullptr;
Shared shared;
httpd_handle_t server = nullptr;
int dns_socket = -1;
std::array<uint8_t, 4> ap_ip{};
bool started = false;
bool ap_up = false;
bool scanning = false;
uint32_t scan_started_ms = 0;
std::array<char, core::kSetupPasswordLength + 1> ap_password{};
bool joining = false;
uint32_t join_started_ms = 0;
core::WifiCredentials join_credentials;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// The web server answers one request at a time, so its buffers are shared. Allocated once, in
// PSRAM, to leave internal RAM for Wi-Fi.
struct Buffers {
  std::array<char, 2048> response{};
  std::array<char, kMaxFormBytes + 1> form{};
};

Buffers* buffers = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

bool AllocateBuffers() {
  if (buffers == nullptr) {
    void* memory = heap_caps_malloc(sizeof(Buffers), MALLOC_CAP_SPIRAM);
    buffers = memory != nullptr ? new (memory) Buffers() : new (std::nothrow) Buffers();
  }
  return buffers != nullptr;
}

class Locked {
 public:
  Locked() { xSemaphoreTake(lock, portMAX_DELAY); }
  Locked(const Locked&) = delete;
  Locked& operator=(const Locked&) = delete;
  Locked(Locked&&) = delete;
  Locked& operator=(Locked&&) = delete;
  ~Locked() { xSemaphoreGive(lock); }
};

esp_err_t SendJson(httpd_req_t* req, const char* json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t OnRoot(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, kWifiSetupPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t OnNetworks(httpd_req_t* req) {
  size_t n = 0;
  buffers->response[n++] = '[';
  {
    Locked hold;
    for (size_t i = 0; i < shared.network_count; ++i) {
      std::array<char, 2 * 6 * core::kMaxSsidBytes + 1> escaped{};
      size_t len = 0;
      if (!core::JsonEscape(shared.networks.at(i).View(), escaped.data(), escaped.size(), &len) ||
          n + len + 4 >= buffers->response.size()) {
        break;
      }
      n += static_cast<size_t>(std::snprintf(&buffers->response.at(n), buffers->response.size() - n,
                                             "%s\"%s\"", i == 0 ? "" : ",", escaped.data()));
    }
  }
  buffers->response[n++] = ']';
  buffers->response[n] = '\0';
  return SendJson(req, buffers->response.data());
}

const char* FormError(core::WifiFormResult result) {
  switch (result) {
    case core::WifiFormResult::kBadSsid:
      return "That network name isn't valid (1-32 characters).";
    case core::WifiFormResult::kBadPassword:
      return "Wi-Fi passwords are 8-63 characters (or empty for an open network).";
    case core::WifiFormResult::kMalformed:
    case core::WifiFormResult::kOk:
      break;
  }
  return "Fill in the network name.";
}

esp_err_t OnJoin(httpd_req_t* req) {
  if (req->content_len > kMaxFormBytes) {
    return SendJson(req, R"({"ok":false,"error":"That's too long."})");
  }
  size_t got = 0;
  while (got < req->content_len) {
    const int n = httpd_req_recv(req, &buffers->form.at(got), req->content_len - got);
    if (n <= 0) {
      return ESP_FAIL;  // connection lost; the server closes it
    }
    got += static_cast<size_t>(n);
  }
  core::WifiCredentials credentials;
  const core::WifiFormResult result =
      core::ParseWifiForm({buffers->form.data(), got}, &credentials);
  if (result != core::WifiFormResult::kOk) {
    std::snprintf(buffers->response.data(), buffers->response.size(),
                  R"({"ok":false,"error":"%s"})", FormError(result));
    return SendJson(req, buffers->response.data());
  }
  {
    Locked hold;
    if (shared.phase != core::WifiSetupPhase::kWaitingForPhone || shared.submission) {
      return SendJson(req, R"({"ok":false,"error":"The dash is busy. Wait a moment."})");
    }
    shared.submitted = credentials;
    shared.submission = true;
  }
  return SendJson(req, R"({"ok":true})");
}

const char* PhaseName(core::WifiSetupPhase phase) {
  switch (phase) {
    case core::WifiSetupPhase::kJoining:
      return "joining";
    case core::WifiSetupPhase::kJoined:
      return "joined";
    case core::WifiSetupPhase::kFailed:
      return "failed";
    default:
      return "waiting";
  }
}

esp_err_t OnStatus(httpd_req_t* req) {
  std::array<char, 2 * 6 * core::kMaxSsidBytes + 1> escaped{};
  size_t len = 0;
  const char* phase = "waiting";
  bool failed = false;
  {
    Locked hold;
    phase = PhaseName(shared.phase);
    failed = shared.last_join_failed;
    static_cast<void>(core::JsonEscape(shared.ssid.View(), escaped.data(), escaped.size(), &len));
  }
  std::snprintf(buffers->response.data(), buffers->response.size(),
                R"({"phase":"%s","ssid":"%s","failed":%s})", phase, escaped.data(),
                failed ? "true" : "false");
  return SendJson(req, buffers->response.data());
}

// Anything else (including phones' captive-portal checks) is sent to the page.
esp_err_t OnAnything(httpd_req_t* req) {
  std::array<char, 32> location{};
  std::snprintf(location.data(), location.size(), "http://%u.%u.%u.%u/", ap_ip[0], ap_ip[1],
                ap_ip[2], ap_ip[3]);
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", location.data());
  return httpd_resp_send(req, nullptr, 0);
}

bool StartHttp() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;  // phones open many connections
  config.stack_size = kHttpStackBytes;
  if (httpd_start(&server, &config) != ESP_OK) {
    server = nullptr;
    return false;
  }
  const std::array<httpd_uri_t, 5> routes = {{
      {"/", HTTP_GET, OnRoot, nullptr},
      {"/networks", HTTP_GET, OnNetworks, nullptr},
      {"/join", HTTP_POST, OnJoin, nullptr},
      {"/status", HTTP_GET, OnStatus, nullptr},
      {"/*", HTTP_GET, OnAnything, nullptr},  // last: wildcard
  }};
  for (const httpd_uri_t& route : routes) {
    httpd_register_uri_handler(server, &route);
  }
  return true;
}

bool StartDns() {
  dns_socket = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (dns_socket < 0) {
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(kDnsPort);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(dns_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
      0) {  // NOLINT
    lwip_close(dns_socket);
    dns_socket = -1;
    return false;
  }
  lwip_fcntl(dns_socket, F_SETFL, O_NONBLOCK);
  return true;
}

void AnswerDns() {
  std::array<uint8_t, core::kDnsMaxPacket> query{};
  std::array<uint8_t, core::kDnsMaxPacket> reply{};
  for (int i = 0; i < kDnsPacketsPerPoll; ++i) {
    sockaddr_in from{};
    socklen_t from_size = sizeof(from);
    const int n = lwip_recvfrom(dns_socket, query.data(), query.size(), 0,
                                reinterpret_cast<sockaddr*>(&from), &from_size);  // NOLINT
    if (n <= 0) {
      return;
    }
    const size_t size = core::BuildCaptiveDnsReply(query.data(), static_cast<size_t>(n), ap_ip,
                                                   reply.data(), reply.size());
    if (size > 0) {
      lwip_sendto(dns_socket, reply.data(), size, 0, reinterpret_cast<sockaddr*>(&from),  // NOLINT
                  from_size);
    }
  }
}

void CollectScan() {
  const int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    return;
  }
  scanning = false;
  if (found < 0) {
    return;  // the scan failed; the page offers typing the name instead
  }
  // Strongest first, each name once.
  std::array<int16_t, kMaxNetworks> rssi{};
  Locked hold;
  shared.network_count = 0;
  for (int16_t i = 0; i < found; ++i) {
    const auto* ap = static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
    const std::string_view name(reinterpret_cast<const char*>(ap->ssid));  // NOLINT
    if (name.empty() || name.size() > core::kMaxSsidBytes) {
      continue;
    }
    bool seen = false;
    for (size_t k = 0; k < shared.network_count; ++k) {
      seen = seen || shared.networks.at(k).View() == name;
    }
    if (seen) {
      continue;
    }
    size_t slot = shared.network_count;
    if (slot == kMaxNetworks) {
      // Full: replace the weakest if this one is stronger.
      slot = static_cast<size_t>(std::min_element(rssi.begin(), rssi.end()) - rssi.begin());
      if (rssi.at(slot) >= ap->rssi) {
        continue;
      }
    } else {
      ++shared.network_count;
    }
    shared.networks.at(slot).Assign(name);
    rssi.at(slot) = ap->rssi;
  }
  for (size_t a = 0; a < shared.network_count; ++a) {  // small: selection sort by RSSI
    for (size_t b = a + 1; b < shared.network_count; ++b) {
      if (rssi.at(b) > rssi.at(a)) {
        std::swap(rssi.at(a), rssi.at(b));
        std::swap(shared.networks.at(a), shared.networks.at(b));
      }
    }
  }
  WiFi.scanDelete();
  ESP_LOGI(kTag, "%u networks found", static_cast<unsigned>(shared.network_count));
}

}  // namespace

// Scanning hops the radio across channels, which makes a running access point drop phones
// and page loads. So Start() scans first, in station mode, and Poll() brings the access point
// up once the scan is done.
bool WifiSetupServer::Start(std::string_view password) {
  Stop();
  if (lock == nullptr) {
    lock = xSemaphoreCreateMutex();
  }
  if (lock == nullptr || !AllocateBuffers()) {
    return false;
  }
  ap_password = {};
  std::memcpy(ap_password.data(), password.data(),
              std::min(password.size(), core::kSetupPasswordLength));
  {
    Locked hold;
    shared = Shared{};
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
  scan_started_ms = millis();
  started = true;
  return true;
}

namespace {

bool StartAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(core::kSetupApName, ap_password.data(), kApChannel, 0, kApMaxClients)) {
    ESP_LOGE(kTag, "could not start the access point");
    return false;
  }
  const IPAddress ip = WiFi.softAPIP();
  ap_ip = {ip[0], ip[1], ip[2], ip[3]};
  if (!StartDns() || !StartHttp()) {
    ESP_LOGE(kTag, "could not start DNS or the web server");
    return false;
  }
  ESP_LOGI(kTag, "access point up at %u.%u.%u.%u", ap_ip[0], ap_ip[1], ap_ip[2], ap_ip[3]);
  return true;
}

}  // namespace

void WifiSetupServer::Stop() {
  if (server != nullptr) {
    httpd_stop(server);
    server = nullptr;
  }
  if (dns_socket >= 0) {
    lwip_close(dns_socket);
    dns_socket = -1;
  }
  if (started || scanning || joining) {
    WiFi.scanDelete();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }
  started = false;
  ap_up = false;
  scanning = false;
  joining = false;
}

WifiSetupServer::Events WifiSetupServer::Poll(uint32_t now_ms, const core::WifiSetup& setup) {
  Events events;
  if (!started) {
    return events;
  }
  if (!ap_up) {
    if (scanning) {
      CollectScan();
    }
    if (!scanning || millis() - scan_started_ms >= kScanTimeoutMs) {
      scanning = false;
      WiFi.scanDelete();
      events.ap_done = true;
      events.ap_ok = StartAccessPoint();
      ap_up = events.ap_ok;
    }
    return events;
  }
  {
    Locked hold;
    shared.phase = setup.Phase();
    shared.last_join_failed = setup.LastJoinFailed();
    shared.ssid.Assign(setup.JoiningSsid());
    if (shared.submission && !joining) {
      shared.submission = false;
      events.submitted = true;
      events.credentials = shared.submitted;
    }
  }
  AnswerDns();
  if (events.submitted) {
    // Joins asynchronously; the access point stays up meanwhile.
    join_credentials = events.credentials;
    WiFi.begin(join_credentials.ssid.CStr(),
               join_credentials.password.Empty() ? nullptr : join_credentials.password.CStr());
    joining = true;
    join_started_ms = now_ms;
    return events;
  }
  if (joining) {
    const wl_status_t status = WiFi.status();
    const uint32_t elapsed = now_ms - join_started_ms;
    const bool failed =
        elapsed >= kStatusSettleMs && (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL);
    if (status == WL_CONNECTED || failed || elapsed >= kJoinTimeoutMs) {
      joining = false;
      events.join_done = true;
      events.join_ok = status == WL_CONNECTED;
      events.credentials = join_credentials;
      if (!events.join_ok) {
        WiFi.disconnect(false, false);  // the station only; keep the access point
      }
      ESP_LOGI(kTag, "join %s", events.join_ok ? "succeeded" : "failed");
    }
  }
  return events;
}

}  // namespace sarathi::ota

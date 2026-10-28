#include "components/hray_native/include/hray_engine.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/asn1_util.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/cert_net/cert_net_fetcher_url_request.h"
#include "net/http/http_network_session.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/quic/quic_context.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_context_getter.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"
#include "third_party/boringssl/src/include/openssl/sha.h"

namespace {

// One Chromium runtime per process. Like NaiveProxy, limits apply to the native
// pool; stream scheduling stays inside Chromium. The embedding library must not
// override malloc, since it is loaded into an already running Rust process.
class Runtime {
 public:
  Runtime() : thread_("hray-cronet") {
    if (!base::CommandLine::InitializedForCurrentProcess())
      base::CommandLine::Init(0, nullptr);
    base::FeatureList::InitInstance("PartitionConnectionsByNetworkIsolationKey",
                                    "");
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("hray-cronet");
    using Type = net::HttpNetworkSession::SocketPoolType;
    net::ClientSocketPoolManager::ConfigureNativeSocketLimits(
        Type::kNormal, 2048, 2040, 2048);
    base::Thread::Options options;
    options.message_pump_type = base::MessagePumpType::IO;
    started_ = thread_.StartWithOptions(std::move(options));
  }

  static Runtime& Get() {
    static base::NoDestructor<Runtime> runtime;
    return *runtime;
  }

  base::AtExitManager at_exit_;
  base::Thread thread_;
  bool started_ = false;
};

// A configured pin is the trust anchor, including for self-signed certificates.
// Match the TLS name as well; BoringSSL still verifies handshake signatures.
class PinnedVerifier final : public net::CertVerifier {
 public:
  explicit PinnedVerifier(net::SHA256HashValue pin) : pin_(pin) {}

  int Verify(const RequestParams& params,
             net::CertVerifyResult* result,
             net::CompletionOnceCallback,
             std::unique_ptr<Request>* request,
             const net::NetLogWithSource&) override {
    request->reset();
    result->Reset();
    result->verified_cert = params.certificate();
    std::string_view spki;
    if (!net::asn1::ExtractSPKIFromDERCert(
            net::x509_util::CryptoBufferAsStringPiece(
                params.certificate()->cert_buffer()), &spki)) {
      result->cert_status = net::CERT_STATUS_INVALID;
      return net::ERR_CERT_INVALID;
    }
    net::SHA256HashValue hash;
    SHA256(reinterpret_cast<const uint8_t*>(spki.data()), spki.size(), hash.data());
    if (hash != pin_) {
      result->cert_status = net::CERT_STATUS_PINNED_KEY_MISSING;
      return net::ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN;
    }
    if (!params.certificate()->VerifyNameMatch(params.hostname())) {
      result->cert_status = net::CERT_STATUS_COMMON_NAME_INVALID;
      return net::ERR_CERT_COMMON_NAME_INVALID;
    }
    result->public_key_hashes.push_back(hash);
    return net::OK;
  }

  void Verify2QwacBinding(
      const std::string&,
      const std::string&,
      const scoped_refptr<net::X509Certificate>&,
      base::OnceCallback<void(const scoped_refptr<net::X509Certificate>&)> callback,
      const net::NetLogWithSource&) override {
    std::move(callback).Run(nullptr);
  }
  // The fixed pin has no mutable trust settings or change notifications.
  void SetConfig(const Config&) override {}
  void AddObserver(Observer*) override {}
  void RemoveObserver(Observer*) override {}

 private:
  net::SHA256HashValue pin_;
};

class Context final : public net::URLRequestContextGetter {
 public:
  explicit Context(scoped_refptr<base::SingleThreadTaskRunner> runner)
      : runner_(std::move(runner)) {}

  void Initialize(std::string name, std::string address, uint16_t port,
                  std::optional<net::SHA256HashValue> pin, bool http3) {
    net::URLRequestContextBuilder builder;
    builder.DisableHttpCache();
    builder.set_proxy_resolution_service(
        net::ConfiguredProxyResolutionService::CreateDirect());
    builder.SetSpdyAndQuicEnabled(true, http3);
    builder.set_host_mapping_rules("MAP " + name + " " + address);
    if (pin) {
      builder.SetCertVerifier(std::make_unique<PinnedVerifier>(*pin));
    } else {
      net::URLRequestContextBuilder cert_builder;
      cert_builder.DisableHttpCache();
      cert_builder.set_proxy_resolution_service(
          net::ConfiguredProxyResolutionService::CreateDirect());
      cert_context_ = cert_builder.Build();
      cert_fetcher_ = base::MakeRefCounted<net::CertNetFetcherURLRequest>();
      cert_fetcher_->SetURLRequestContext(cert_context_.get());
      builder.SetCertVerifier(net::CertVerifier::CreateDefault(cert_fetcher_));
    }
    if (http3) {
      auto quic = std::make_unique<net::QuicContext>();
      quic->params()->origins_to_force_quic_on.insert(
          url::SchemeHostPort("https", name, port));
      builder.set_quic_context(std::move(quic));
    }
    context_ = builder.Build();
  }

  net::URLRequestContext* GetURLRequestContext() override {
    return context_.get();
  }
  scoped_refptr<base::SingleThreadTaskRunner> GetNetworkTaskRunner() const override {
    return runner_;
  }

 private:
  ~Context() override {
    auto context = std::move(context_);
    NotifyContextShuttingDown();
    context.reset();
    if (cert_fetcher_)
      cert_fetcher_->Shutdown();
  }
  scoped_refptr<base::SingleThreadTaskRunner> runner_;
  std::unique_ptr<net::URLRequestContext> cert_context_;
  scoped_refptr<net::CertNetFetcherURLRequest> cert_fetcher_;
  std::unique_ptr<net::URLRequestContext> context_;
};

}  // namespace

stream_engine* hray_cronet_engine_create(const char* server_name,
                                         const char* address,
                                         uint16_t port,
                                         const uint8_t* pin_sha256,
                                         bool http3) {
  if (!server_name || !address || !*address || !port)
    return nullptr;
  GURL origin(std::string("https://") + server_name);
  if (!origin.is_valid() || origin.host().empty() || origin.has_username() ||
      origin.has_password() || origin.has_port() || origin.path() != "/")
    return nullptr;
  auto& runtime = Runtime::Get();
  if (!runtime.started_)
    return nullptr;
  std::optional<net::SHA256HashValue> pin;
  if (pin_sha256) {
    pin.emplace();
    std::copy_n(pin_sha256, pin->size(), pin->begin());
  }
  auto context = base::MakeRefCounted<Context>(runtime.thread_.task_runner());
  base::WaitableEvent initialized;
  runtime.thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(
          [](scoped_refptr<Context> context, std::string name,
             std::string address, uint16_t port,
             std::optional<net::SHA256HashValue> pin,
             bool http3, base::WaitableEvent* initialized) {
            context->Initialize(std::move(name), std::move(address), port, pin, http3);
            initialized->Signal();
          }, context, std::string(origin.host()), std::string(address), port, pin, http3,
          &initialized));
  initialized.Wait();
  auto* engine = new stream_engine;
  engine->obj = context.release();
  engine->annotation = nullptr;
  return engine;
}

void hray_cronet_engine_destroy(stream_engine* engine) {
  if (!engine)
    return;
  static_cast<Context*>(engine->obj)->Release();
  delete engine;
}

#include "components/xray_native/include/xray_engine.h"

#include <algorithm>
#include <memory>
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
#include "net/quic/quic_context.h"
#include "net/socket/client_socket_pool.h"
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
  Runtime() : thread_("xray-cronet") {
    if (!base::CommandLine::InitializedForCurrentProcess())
      base::CommandLine::Init(0, nullptr);
    base::FeatureList::InitInstance("PartitionConnectionsByNetworkIsolationKey",
                                    "");
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("xray-cronet");
    using Type = net::HttpNetworkSession::SocketPoolType;
    net::ClientSocketPoolManager::ConfigureNativeSocketLimits(
        Type::kNormal, 2048, 2040, 2048);
    net::ClientSocketPool::set_used_idle_socket_timeout(base::Seconds(60));
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

// Apply the leaf pin before delegating to Chromium's ordinary CA/name verifier.
// Keeping the verifier's request handle unchanged preserves cancellation and
// asynchronous completion without introducing another verification cache.
class PinnedVerifier final : public net::CertVerifier {
 public:
  PinnedVerifier(std::unique_ptr<net::CertVerifier> verifier,
                 net::SHA256HashValue pin)
      : verifier_(std::move(verifier)), pin_(pin) {}

  int Verify(const RequestParams& params,
             net::CertVerifyResult* result,
             net::CompletionOnceCallback callback,
             std::unique_ptr<Request>* request,
             const net::NetLogWithSource& log) override {
    std::string_view spki;
    if (!net::asn1::ExtractSPKIFromDERCert(
            net::x509_util::CryptoBufferAsStringPiece(
                params.certificate()->cert_buffer()), &spki)) {
      result->Reset();
      result->cert_status = net::CERT_STATUS_INVALID;
      return net::ERR_CERT_INVALID;
    }
    net::SHA256HashValue hash;
    SHA256(reinterpret_cast<const uint8_t*>(spki.data()), spki.size(), hash.data());
    if (hash != pin_) {
      result->Reset();
      result->cert_status = net::CERT_STATUS_PINNED_KEY_MISSING;
      return net::ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN;
    }
    return verifier_->Verify(params, result, std::move(callback), request, log);
  }

  void Verify2QwacBinding(
      const std::string& binding,
      const std::string& hostname,
      const scoped_refptr<net::X509Certificate>& cert,
      base::OnceCallback<void(const scoped_refptr<net::X509Certificate>&)> callback,
      const net::NetLogWithSource& log) override {
    verifier_->Verify2QwacBinding(binding, hostname, cert, std::move(callback), log);
  }
  void SetConfig(const Config& config) override { verifier_->SetConfig(config); }
  void AddObserver(Observer* observer) override { verifier_->AddObserver(observer); }
  void RemoveObserver(Observer* observer) override { verifier_->RemoveObserver(observer); }

 private:
  std::unique_ptr<net::CertVerifier> verifier_;
  net::SHA256HashValue pin_;
};

class Context final : public net::URLRequestContextGetter {
 public:
  explicit Context(scoped_refptr<base::SingleThreadTaskRunner> runner)
      : runner_(std::move(runner)) {}

  void Initialize(std::string name, std::string address, uint16_t port,
                  net::SHA256HashValue pin, bool http3) {
    net::URLRequestContextBuilder cert_builder;
    cert_builder.DisableHttpCache();
    cert_context_ = cert_builder.Build();
    cert_fetcher_ = base::MakeRefCounted<net::CertNetFetcherURLRequest>();
    cert_fetcher_->SetURLRequestContext(cert_context_.get());
    net::URLRequestContextBuilder builder;
    builder.DisableHttpCache();
    builder.SetSpdyAndQuicEnabled(true, http3);
    builder.set_host_mapping_rules("MAP " + name + " " + address);
    builder.SetCertVerifier(std::make_unique<PinnedVerifier>(
        net::CertVerifier::CreateDefault(cert_fetcher_), pin));
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
    cert_fetcher_->Shutdown();
  }
  scoped_refptr<base::SingleThreadTaskRunner> runner_;
  std::unique_ptr<net::URLRequestContext> cert_context_;
  scoped_refptr<net::CertNetFetcherURLRequest> cert_fetcher_;
  std::unique_ptr<net::URLRequestContext> context_;
};

}  // namespace

stream_engine* xray_cronet_engine_create(const char* server_name,
                                         const char* address,
                                         uint16_t port,
                                         const uint8_t* pin_sha256,
                                         bool http3) {
  if (!server_name || !address || !*address || !port || !pin_sha256)
    return nullptr;
  GURL origin(std::string("https://") + server_name);
  if (!origin.is_valid() || origin.host().empty() || origin.has_username() ||
      origin.has_password() || origin.has_port() || origin.path() != "/")
    return nullptr;
  auto& runtime = Runtime::Get();
  if (!runtime.started_)
    return nullptr;
  net::SHA256HashValue pin;
  std::copy_n(pin_sha256, pin.size(), pin.begin());
  auto context = base::MakeRefCounted<Context>(runtime.thread_.task_runner());
  base::WaitableEvent initialized;
  runtime.thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(
          [](scoped_refptr<Context> context, std::string name,
             std::string address, uint16_t port, net::SHA256HashValue pin,
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

void xray_cronet_engine_destroy(stream_engine* engine) {
  if (!engine)
    return;
  static_cast<Context*>(engine->obj)->Release();
  delete engine;
}

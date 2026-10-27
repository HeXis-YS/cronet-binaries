// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_split.h"
#include "base/task/single_thread_task_runner.h"
#include "components/xray_native/include/xray_stream.h"
#include "net/base/http_user_agent_settings.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/schemeful_site.h"
#include "net/http/bidirectional_stream.h"
#include "net/http/bidirectional_stream_request_info.h"
#include "net/http/http_transaction_factory.h"
#include "net/http/http_util.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "url/gurl.h"

namespace {

// Owns split header strings only for the duration of the C callback.
class HeadersArray {
 public:
  explicit HeadersArray(const quiche::HttpHeaderBlock& block) {
    for (const auto& entry : block) {
      std::string value(entry.second);
      size_t start = 0;
      do {
        size_t end = value.find('\0', start);
        strings_.emplace_back(std::string(entry.first),
                              value.substr(start, end - start));
        if (end == std::string::npos) {
          break;
        }
        start = end + 1;
      } while (true);
    }
    for (const auto& entry : strings_) {
      headers_.push_back({entry.first.c_str(), entry.second.c_str()});
    }
    array = {headers_.size(), headers_.data()};
  }
  bidirectional_stream_header_array array;

 private:
  base::StringPairs strings_;
  std::vector<bidirectional_stream_header> headers_;
};

class Stream final : public net::BidirectionalStream::Delegate {
 public:
  Stream(stream_engine* engine,
         void* annotation,
         const bidirectional_stream_callback& callbacks)
      : context_(static_cast<net::URLRequestContextGetter*>(engine->obj)),
        runner_(context_->GetNetworkTaskRunner()),
        callbacks_(callbacks),
        handle_{this, annotation} {
    weak_this_ = weak_factory_.GetWeakPtr();
  }

  ~Stream() override = default;
  bidirectional_stream* handle() { return &handle_; }

  void Start(std::unique_ptr<net::BidirectionalStreamRequestInfo> request) {
    runner_->PostTask(FROM_HERE,
                      base::BindOnce(&Stream::StartOnNetworkThread, weak_this_,
                                     std::move(request)));
  }
  void Read(scoped_refptr<net::WrappedIOBuffer> buffer, int capacity) {
    runner_->PostTask(FROM_HERE,
                      base::BindOnce(&Stream::ReadOnNetworkThread, weak_this_,
                                     std::move(buffer), capacity));
  }
  void Write(scoped_refptr<net::WrappedIOBuffer> buffer, int count, bool end) {
    runner_->PostTask(FROM_HERE,
                      base::BindOnce(&Stream::WriteOnNetworkThread, weak_this_,
                                     std::move(buffer), count, end));
  }
  void Cancel() {
    runner_->PostTask(FROM_HERE, base::BindOnce(&Stream::Finish, weak_this_,
                                                net::ERR_ABORTED));
  }
  void Destroy() {
    // A delegate must not delete Chromium's callback stack inline.
    runner_->DeleteSoon(FROM_HERE, this);
  }

 private:
  void StartOnNetworkThread(
      std::unique_ptr<net::BidirectionalStreamRequestInfo> request) {
    auto* context = context_->GetURLRequestContext();
    if (context->http_user_agent_settings()) {
      request->extra_headers.SetHeaderIfMissing(
          net::HttpRequestHeaders::kUserAgent,
          context->http_user_agent_settings()->GetUserAgent());
    }
    write_done_ = request->end_stream_on_headers;
    stream_ = std::make_unique<net::BidirectionalStream>(
        std::move(request), context->http_transaction_factory()->GetSession(),
        true, this);
  }
  void ReadOnNetworkThread(scoped_refptr<net::WrappedIOBuffer> buffer,
                           int capacity) {
    if (!headers_received_ || read_buffer_ || read_done_) {
      Finish(net::ERR_UNEXPECTED);
      return;
    }
    read_buffer_ = std::move(buffer);
    int result = stream_->ReadData(read_buffer_.get(), capacity);
    if (result == net::ERR_IO_PENDING) {
      return;
    }
    if (result < 0) {
      Finish(result);
    } else {
      OnDataRead(result);
    }
  }
  void WriteOnNetworkThread(scoped_refptr<net::WrappedIOBuffer> buffer,
                            int count,
                            bool end) {
    if (!ready_ || write_buffer_ || write_done_) {
      Finish(net::ERR_UNEXPECTED);
      return;
    }
    write_buffer_ = std::move(buffer);
    write_done_ = end;
    stream_->SendvData({write_buffer_}, {count}, end);
  }
  void OnStreamReady(bool request_headers_sent) override {
    if (done_) {
      return;
    }
    ready_ = true;
    if (!request_headers_sent) {
      stream_->SendRequestHeaders();
    }
    callbacks_.ready(&handle_);
  }
  void OnHeadersReceived(const quiche::HttpHeaderBlock& headers,
                         const net::ProxyInfo&) override {
    if (done_) {
      return;
    }
    headers_received_ = true;
    HeadersArray array(headers);
    callbacks_.headers(&handle_, &array.array);
  }
  void OnDataRead(int count) override {
    if (done_) {
      return;
    }
    auto buffer = std::move(read_buffer_);
    read_done_ = count == 0;
    callbacks_.read(&handle_, buffer->data(), count);
    MaybeFinish();
  }
  void OnDataSent() override {
    if (done_) {
      return;
    }
    write_buffer_ = nullptr;
    callbacks_.written(&handle_);
    MaybeFinish();
  }
  void OnTrailersReceived(const quiche::HttpHeaderBlock&) override {}
  void OnFailed(int error) override { Finish(error); }
  void MaybeFinish() {
    if (read_done_ && write_done_ && !write_buffer_) {
      Finish(net::OK);
    }
  }
  void Finish(int error) {
    if (done_) {
      return;
    }
    done_ = true;
    weak_factory_.InvalidateWeakPtrs();
    // Leave Chromium's callback stack before deleting the transport. Keep Rust
    // buffers alive until I/O has actually stopped, including cancellation.
    runner_->PostTask(FROM_HERE,
                      base::BindOnce(&Stream::CompleteOnNetworkThread,
                                     base::Unretained(this), error));
  }
  void CompleteOnNetworkThread(int error) {
    stream_.reset();
    read_buffer_ = nullptr;
    write_buffer_ = nullptr;
    callbacks_.done(&handle_, error);
  }

  const scoped_refptr<net::URLRequestContextGetter> context_;
  const scoped_refptr<base::SingleThreadTaskRunner> runner_;
  const bidirectional_stream_callback callbacks_;
  bidirectional_stream handle_;
  std::unique_ptr<net::BidirectionalStream> stream_;
  scoped_refptr<net::WrappedIOBuffer> read_buffer_;
  scoped_refptr<net::WrappedIOBuffer> write_buffer_;
  bool ready_ = false;
  bool headers_received_ = false;
  bool read_done_ = false;
  bool write_done_ = false;
  bool done_ = false;
  // Only the network thread dereferences or invalidates these weak pointers.
  base::WeakPtr<Stream> weak_this_;
  base::WeakPtrFactory<Stream> weak_factory_{this};
};

Stream* GetStream(bidirectional_stream* stream) {
  return static_cast<Stream*>(stream->obj);
}
}  // namespace

bidirectional_stream* xray_stream_create(
    stream_engine* engine,
    void* annotation,
    const bidirectional_stream_callback* callbacks) {
  return (new Stream(engine, annotation, *callbacks))->handle();
}
void xray_stream_destroy(bidirectional_stream* stream) {
  GetStream(stream)->Destroy();
}
int xray_stream_start(bidirectional_stream* stream,
                      const char* url,
                      const char* method,
                      const bidirectional_stream_header_array* headers,
                      const char* isolation,
                      bool end_of_stream) {
  auto request = std::make_unique<net::BidirectionalStreamRequestInfo>();
  request->url = GURL(url);
  request->method = method;
  request->priority = net::MEDIUM;
  net::SchemefulSite site{GURL(isolation)};
  if (!request->url.is_valid() || !request->url.SchemeIs("https") ||
      !net::HttpUtil::IsValidHeaderName(method) || site.opaque()) {
    return -1;
  }
  request->network_isolation_key = net::NetworkIsolationKey(site, site);
  request->network_anonymization_key =
      net::NetworkAnonymizationKey::CreateSameSite(site);
  for (size_t i = 0; i < headers->count; ++i) {
    // The C caller supplies count entries, alive until this call returns.
    const auto& header = UNSAFE_BUFFERS(headers->headers[i]);
    if (!net::HttpUtil::IsValidHeaderName(header.key) ||
        !net::HttpUtil::IsValidHeaderValue(header.value)) {
      return -1;
    }
    request->extra_headers.SetHeader(header.key, header.value);
  }
  if (auto authority = request->extra_headers.GetHeader("host")) {
    request->authority = *authority;
    request->extra_headers.RemoveHeader("host");
  }
  request->end_stream_on_headers = end_of_stream;
  GetStream(stream)->Start(std::move(request));
  return 0;
}
int xray_stream_read(bidirectional_stream* stream, char* buffer, int capacity) {
  if (!buffer || capacity <= 0) {
    return 0;
  }
  auto wrapped = base::MakeRefCounted<net::WrappedIOBuffer>(
      UNSAFE_BUFFERS(base::span(buffer, static_cast<size_t>(capacity))));
  GetStream(stream)->Read(std::move(wrapped), capacity);
  return 1;
}
int xray_stream_write(bidirectional_stream* stream,
                      const char* buffer,
                      int count,
                      bool end_of_stream) {
  if (!buffer || count < 0) {
    return 0;
  }
  auto wrapped = base::MakeRefCounted<net::WrappedIOBuffer>(
      UNSAFE_BUFFERS(base::span(buffer, static_cast<size_t>(count))));
  GetStream(stream)->Write(std::move(wrapped), count, end_of_stream);
  return 1;
}
void xray_stream_cancel(bidirectional_stream* stream) {
  GetStream(stream)->Cancel();
}

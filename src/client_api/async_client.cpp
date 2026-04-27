#include "client_api/async_client.h"

#include <utility>

namespace mqtt {

AsyncClient::AsyncClient(std::string client_id,
                         ReconnectBackoffPolicy reconnect_backoff)
  : AsyncClient(ClientConfig{.client_id = std::move(client_id),
                 .reconnect_backoff = reconnect_backoff}) {}

AsyncClient::AsyncClient(ClientConfig client_config)
  : sync_client_(std::move(client_config)),
      dispatch_thread_([this]() { run_dispatch_loop(); }) {}

AsyncClient::~AsyncClient() {
  {
    std::lock_guard<std::mutex> queue_guard(queue_mutex_);
    stop_requested_ = true;
  }
  queue_condition_.notify_all();
  if (dispatch_thread_.joinable()) {
    dispatch_thread_.join();
  }
}

void AsyncClient::set_sync_callbacks(SyncClientCallbacks callbacks) {
  std::lock_guard<std::mutex> state_guard(state_mutex_);
  sync_client_.set_callbacks(std::move(callbacks));
}

void AsyncClient::set_message_handler(MessageHandler message_handler) {
  std::lock_guard<std::mutex> state_guard(message_handler_mutex_);
  message_handler_ = std::move(message_handler);
}

void AsyncClient::async_connect(const ConnectPacket &connect_packet,
                                ConnectCompletion completion,
                                uint32_t timeout_ms) {
  enqueue_task([this, connect_packet, completion = std::move(completion),
                timeout_ms]() mutable {
    try {
      std::optional<ConnectionNegotiationResult> connect_result;
      {
        std::lock_guard<std::mutex> state_guard(state_mutex_);
        const uint32_t effective_timeout_ms =
            timeout_ms == 0U
                ? sync_client_.client_config().operation_timeouts.connect_ms
                : timeout_ms;
        connect_result = sync_client_.connect(connect_packet, effective_timeout_ms);
      }
      if (completion) {
        completion(connect_result, std::nullopt);
      }
    } catch (const ClientApiException &exception) {
      if (completion) {
        completion(std::nullopt, exception.error());
      }
    } catch (const ClientException &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    } catch (const std::exception &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    }
  });
}

void AsyncClient::async_connect(ConnectCompletion completion) {
  enqueue_task([this, completion = std::move(completion)]() mutable {
    try {
      std::optional<ConnectionNegotiationResult> connect_result;
      {
        std::lock_guard<std::mutex> state_guard(state_mutex_);
        connect_result = sync_client_.connect();
      }
      if (completion) {
        completion(connect_result, std::nullopt);
      }
    } catch (const ClientApiException &exception) {
      if (completion) {
        completion(std::nullopt, exception.error());
      }
    } catch (const ClientException &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    } catch (const std::exception &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    }
  });
}

void AsyncClient::async_publish(const Message &message,
                                PublishCompletion completion,
                                uint32_t timeout_ms) {
  enqueue_task([this, message, completion = std::move(completion),
                timeout_ms]() mutable {
    try {
      std::optional<ReasonCode> publish_result;
      {
        std::lock_guard<std::mutex> state_guard(state_mutex_);
        const uint32_t effective_timeout_ms =
            timeout_ms == 0U
                ? sync_client_.client_config().operation_timeouts.publish_ms
                : timeout_ms;
        publish_result = sync_client_.publish(message, effective_timeout_ms);
      }
      if (completion) {
        completion(publish_result, std::nullopt);
      }
    } catch (const ClientApiException &exception) {
      if (completion) {
        completion(std::nullopt, exception.error());
      }
    } catch (const ClientException &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    } catch (const std::exception &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    }
  });
}

void AsyncClient::async_subscribe(
    const std::vector<AsyncSubscribeRequest> &requests,
    SubscribeCompletion completion, uint32_t timeout_ms) {
  enqueue_task([this, requests, completion = std::move(completion),
                timeout_ms]() mutable {
    std::vector<ClientSubscriptionManager::SubscribeRequest> subscribe_requests;
    subscribe_requests.reserve(requests.size());
    for (const AsyncSubscribeRequest &request : requests) {
      ClientSubscriptionManager::SubscribeRequest subscribe_request;
      subscribe_request.topic_filter = request.topic_filter;
      subscribe_request.requested_qos = request.requested_qos;
      subscribe_request.options = request.options;
      subscribe_request.callback =
          [this](const PublishPacket &publish_packet) {
            invoke_message_handler(publish_packet);
          };
      subscribe_requests.push_back(std::move(subscribe_request));
    }

    try {
      std::optional<std::vector<ReasonCode>> subscribe_result;
      {
        std::lock_guard<std::mutex> state_guard(state_mutex_);
        const uint32_t effective_timeout_ms =
            timeout_ms == 0U
                ? sync_client_.client_config().operation_timeouts.subscribe_ms
                : timeout_ms;
        subscribe_result =
            sync_client_.subscribe(subscribe_requests, effective_timeout_ms);
      }
      if (completion) {
        completion(subscribe_result, std::nullopt);
      }
    } catch (const ClientApiException &exception) {
      if (completion) {
        completion(std::nullopt, exception.error());
      }
    } catch (const ClientException &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    } catch (const std::exception &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    }
  });
}

void AsyncClient::async_unsubscribe(const std::vector<std::string> &topic_filters,
                                    UnsubscribeCompletion completion,
                                    uint32_t timeout_ms) {
  enqueue_task([this, topic_filters, completion = std::move(completion),
                timeout_ms]() mutable {
    try {
      std::optional<std::vector<ReasonCode>> unsubscribe_result;
      {
        std::lock_guard<std::mutex> state_guard(state_mutex_);
        const uint32_t effective_timeout_ms =
            timeout_ms == 0U
                ? sync_client_.client_config().operation_timeouts.unsubscribe_ms
                : timeout_ms;
        unsubscribe_result =
            sync_client_.unsubscribe(topic_filters, effective_timeout_ms);
      }
      if (completion) {
        completion(unsubscribe_result, std::nullopt);
      }
    } catch (const ClientApiException &exception) {
      if (completion) {
        completion(std::nullopt, exception.error());
      }
    } catch (const ClientException &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    } catch (const std::exception &exception) {
      if (completion) {
        completion(std::nullopt, to_async_error(exception));
      }
    }
  });
}

void AsyncClient::async_disconnect(ReasonCode reason_code) {
  enqueue_task([this, reason_code]() {
    std::lock_guard<std::mutex> state_guard(state_mutex_);
    sync_client_.disconnect(reason_code);
  });
}

void AsyncClient::on_inbound_publish(const PublishPacket &publish_packet) {
  enqueue_task([this, publish_packet]() {
    std::lock_guard<std::mutex> state_guard(state_mutex_);
    (void)sync_client_.dispatch_inbound_publish(publish_packet);
  });
}

bool AsyncClient::is_connected() const {
  std::lock_guard<std::mutex> state_guard(state_mutex_);
  return sync_client_.is_connected();
}

bool AsyncClient::has_subscription(std::string_view topic_filter) const {
  std::lock_guard<std::mutex> state_guard(state_mutex_);
  return sync_client_.has_subscription(topic_filter);
}

ClientConfig AsyncClient::client_config() const {
  std::lock_guard<std::mutex> state_guard(state_mutex_);
  return sync_client_.client_config();
}

void AsyncClient::enqueue_task(DispatchTask task) {
  {
    std::lock_guard<std::mutex> queue_guard(queue_mutex_);
    if (stop_requested_) {
      return;
    }
    dispatch_queue_.push_back(std::move(task));
  }
  queue_condition_.notify_one();
}

void AsyncClient::run_dispatch_loop() {
  while (true) {
    DispatchTask next_task;
    {
      std::unique_lock<std::mutex> queue_lock(queue_mutex_);
      queue_condition_.wait(queue_lock, [this]() {
        return stop_requested_ || !dispatch_queue_.empty();
      });

      if (stop_requested_ && dispatch_queue_.empty()) {
        return;
      }

      next_task = std::move(dispatch_queue_.front());
      dispatch_queue_.pop_front();
    }

    if (next_task) {
      next_task();
    }
  }
}

void AsyncClient::invoke_message_handler(const PublishPacket &publish_packet) {
  MessageHandler handler_copy;
  {
    std::lock_guard<std::mutex> state_guard(message_handler_mutex_);
    handler_copy = message_handler_;
  }

  if (handler_copy) {
    handler_copy(publish_packet);
  }
}

AsyncOperationError AsyncClient::to_async_error(const ClientException &exception) {
  return client_api_error_from_client_exception(exception);
}

AsyncOperationError AsyncClient::to_async_error(const std::exception &exception) {
  return client_api_error_from_std_exception(exception);
}

} // namespace mqtt

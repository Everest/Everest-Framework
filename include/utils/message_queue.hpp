// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2022 Pionix GmbH and Contributors to EVerest
#ifndef UTILS_MESSAGE_QUEUE_HPP
#define UTILS_MESSAGE_QUEUE_HPP

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <utils/types.hpp>

namespace Everest {
using json = nlohmann::json;

/// \brief Contains a payload and the topic it was received on
struct Message {
    std::string topic;   ///< The MQTT topic where this message originated from
    std::string payload; ///< The message payload
};

/// \brief Simple message queue that takes std::string messages, parsed them and dispatches them to handlers
class MessageQueue {
private:
    std::thread worker_thread;
    std::queue<std::shared_ptr<Message>> message_queue;
    std::mutex message_mutex;
    std::function<void(std::shared_ptr<Message> message)> message_callback;
    std::condition_variable cv;
    bool running;

public:
    /// \brief Creates a message queue with the provided \p message_callback
    MessageQueue(const std::function<void(std::shared_ptr<Message> message)>& message_callback);

    /// \brief Adds a \p message to the message queue which will then be delivered to the message callback
    void add(std::shared_ptr<Message> message);

    /// \brief Stops the message queue
    void stop();
};

/// \brief Contains a message queue driven list of handler callbacks
class MessageHandler {
private:
    std::vector<std::shared_ptr<Handler>> handlers;
    std::thread handler_thread;
    std::queue<std::shared_ptr<json>> message_queue;
    std::mutex message_mutex;
    std::mutex handlers_mutex;
    std::condition_variable cv;
    bool running;

public:
    /// \brief Creates the message handler
    MessageHandler();

    /// \brief Adds a \p message to the message queue which will be delivered to the registered handlers
    void add(std::shared_ptr<json> message);

    /// \brief Stops the message handler
    void stop();

    /// \brief Adds a \p handler that will receive messages from the queue. This function can be called multiple times
    /// to add multiple handlers
    void add_handler(std::shared_ptr<Handler> handler);

    /// \brief Removes a specific \p handler
    void remove_handler(std::shared_ptr<Handler> handler);

    /// \brief \returns the number of registered handlers
    size_t count_handlers();
};

} // namespace Everest

#endif // UTILS_MESSAGE_QUEUE_HPP
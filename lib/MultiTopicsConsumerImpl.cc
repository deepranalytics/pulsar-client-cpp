/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "MultiTopicsConsumerImpl.h"

#include <chrono>
#include <stdexcept>

#include "ClientImpl.h"
#include "ConsumerImpl.h"
#include "ExecutorService.h"
#include "LogUtils.h"
#include "LookupService.h"
#include "MessageImpl.h"
#include "MessagesImpl.h"
#include "MultiTopicsBrokerConsumerStatsImpl.h"
#include "TopicName.h"
#include "UnAckedMessageTrackerDisabled.h"
#include "UnAckedMessageTrackerEnabled.h"

DECLARE_LOG_OBJECT()

using namespace pulsar;

using std::chrono::milliseconds;
using std::chrono::seconds;

MultiTopicsConsumerImpl::MultiTopicsConsumerImpl(const ClientImplPtr& client, const TopicNamePtr& topicName,
                                                 int numPartitions, const std::string& subscriptionName,
                                                 const ConsumerConfiguration& conf,
                                                 const LookupServicePtr& lookupServicePtr,
                                                 const ConsumerInterceptorsPtr& interceptors,
                                                 Commands::SubscriptionMode subscriptionMode,
                                                 const boost::optional<MessageId>& startMessageId)
    : MultiTopicsConsumerImpl(client, {topicName->toString()}, subscriptionName, topicName, conf,
                              lookupServicePtr, interceptors, subscriptionMode, startMessageId) {
    topicsPartitions_[topicName->toString()] = numPartitions;
}

MultiTopicsConsumerImpl::MultiTopicsConsumerImpl(
    const ClientImplPtr& client, const std::vector<std::string>& topics, const std::string& subscriptionName,
    const TopicNamePtr& topicName, const ConsumerConfiguration& conf,
    const LookupServicePtr& lookupServicePtr, const ConsumerInterceptorsPtr& interceptors,
    Commands::SubscriptionMode subscriptionMode, const boost::optional<MessageId>& startMessageId)
    : ConsumerImplBase(client, topicName ? topicName->toString() : "EmptyTopics",
                       Backoff(milliseconds(100), seconds(60), milliseconds(0)), conf,
                       client->getListenerExecutorProvider()->get()),
      client_(client),
      subscriptionName_(subscriptionName),
      conf_(conf),
      incomingMessages_(conf.getReceiverQueueSize()),
      messageListener_(conf.getMessageListener()),
      lookupServicePtr_(lookupServicePtr),
      numberTopicPartitions_(std::make_shared<std::atomic<int>>(0)),
      topics_(topics),
      subscriptionMode_(subscriptionMode),
      startMessageId_(startMessageId),
      interceptors_(interceptors) {
    std::stringstream consumerStrStream;
    consumerStrStream << "[Muti Topics Consumer: "
                      << "TopicName - " << topic() << " - Subscription - " << subscriptionName << "]";
    consumerStr_ = consumerStrStream.str();

    if (conf.getUnAckedMessagesTimeoutMs() != 0) {
        if (conf.getTickDurationInMs() > 0) {
            unAckedMessageTrackerPtr_.reset(new UnAckedMessageTrackerEnabled(
                conf.getUnAckedMessagesTimeoutMs(), conf.getTickDurationInMs(), client, *this));
        } else {
            unAckedMessageTrackerPtr_.reset(
                new UnAckedMessageTrackerEnabled(conf.getUnAckedMessagesTimeoutMs(), client, *this));
        }
    } else {
        unAckedMessageTrackerPtr_.reset(new UnAckedMessageTrackerDisabled());
    }
    unAckedMessageTrackerPtr_->start();
    auto partitionsUpdateInterval = static_cast<unsigned int>(client->conf().getPartitionsUpdateInterval());
    if (partitionsUpdateInterval > 0) {
        partitionsUpdateTimer_ = listenerExecutor_->createDeadlineTimer();
        partitionsUpdateInterval_ = seconds(partitionsUpdateInterval);
        lookupServicePtr_ = client->getLookup();
    }

    state_ = Pending;
}

void MultiTopicsConsumerImpl::start() {
    if (topics_.empty()) {
        State state = Pending;
        if (state_.compare_exchange_strong(state, Ready)) {
            LOG_DEBUG("No topics passed in when create MultiTopicsConsumer.");
            multiTopicsConsumerCreatedPromise_.setValue(get_shared_this_ptr());
            return;
        } else {
            LOG_ERROR("Consumer " << consumerStr_ << " in wrong state: " << state_);
            multiTopicsConsumerCreatedPromise_.setFailed(ResultUnknownError);
            return;
        }
    }

    // start call subscribeOneTopicAsync for each single topic
    int topicsNumber = topics_.size();
    std::shared_ptr<std::atomic<int>> topicsNeedCreate = std::make_shared<std::atomic<int>>(topicsNumber);
    // subscribe for each passed in topic
    auto weakSelf = weak_from_this();
    for (std::vector<std::string>::const_iterator itr = topics_.begin(); itr != topics_.end(); itr++) {
        const auto& topic = *itr;
        subscribeOneTopicAsync(topic).addListener(
            [this, weakSelf, topic, topicsNeedCreate](Result result, const Consumer& consumer) {
                auto self = weakSelf.lock();
                if (self) {
                    handleOneTopicSubscribed(result, consumer, topic, topicsNeedCreate);
                }
            });
    }
}

void MultiTopicsConsumerImpl::handleOneTopicSubscribed(
    Result result, const Consumer& consumer, const std::string& topic,
    const std::shared_ptr<std::atomic<int>>& topicsNeedCreate) {
    if (result != ResultOk) {
        state_ = Failed;
        // Use the first failed result
        auto expectedResult = ResultOk;
        failedResult.compare_exchange_strong(expectedResult, result);
        LOG_ERROR("Failed when subscribed to topic " << topic << " in TopicsConsumer. Error - " << result);
    } else {
        LOG_DEBUG("Subscribed to topic " << topic << " in TopicsConsumer ");
    }

    if (--(*topicsNeedCreate) == 0) {
        State state = Pending;
        if (state_.compare_exchange_strong(state, Ready)) {
            LOG_INFO("Successfully Subscribed to Topics");
            multiTopicsConsumerCreatedPromise_.setValue(get_shared_this_ptr());
            // Now all child topics are successfully subscribed, start messageListeners
            if (messageListener_ && !conf_.isStartPaused()) {
                LOG_INFO("Start messageListeners");
                resumeMessageListener();
            }
        } else {
            LOG_ERROR("Unable to create Consumer - " << consumerStr_ << " Error - " << result);
            // unsubscribed all of the successfully subscribed partitioned consumers
            // `shutdown()`, which set multiTopicsConsumerCreatedPromise_ with `failedResult`, will be called
            // when `closeAsync` completes.
            closeAsync(nullptr);
        }
    }
}

// subscribe for passed in topic
Future<Result, Consumer> MultiTopicsConsumerImpl::subscribeOneTopicAsync(const std::string& topic) {
    TopicNamePtr topicName;
    ConsumerSubResultPromisePtr topicPromise = std::make_shared<Promise<Result, Consumer>>();
    if (!(topicName = TopicName::get(topic))) {
        LOG_ERROR("TopicName invalid: " << topic);
        topicPromise->setFailed(ResultInvalidTopicName);
        return topicPromise->getFuture();
    }

    const auto state = state_.load();
    if (state == Closed || state == Closing) {
        LOG_ERROR("MultiTopicsConsumer already closed when subscribe.");
        topicPromise->setFailed(ResultAlreadyClosed);
        return topicPromise->getFuture();
    }

    // subscribe for each partition, when all partitions completed, complete promise
    Lock lock(mutex_);
    auto entry = topicsPartitions_.find(topic);
    if (entry == topicsPartitions_.end()) {
        lock.unlock();
        lookupServicePtr_->getPartitionMetadataAsync(topicName).addListener(
            [this, topicName, topicPromise](Result result, const LookupDataResultPtr& lookupDataResult) {
                if (result != ResultOk) {
                    LOG_ERROR("Error Checking/Getting Partition Metadata while MultiTopics Subscribing- "
                              << consumerStr_ << " result: " << result)
                    topicPromise->setFailed(result);
                    return;
                }
                subscribeTopicPartitions(lookupDataResult->getPartitions(), topicName, subscriptionName_,
                                         topicPromise);
            });
    } else {
        auto numPartitions = entry->second;
        lock.unlock();
        subscribeTopicPartitions(numPartitions, topicName, subscriptionName_, topicPromise);
    }
    return topicPromise->getFuture();
}

void MultiTopicsConsumerImpl::subscribeTopicPartitions(
    int numPartitions, const TopicNamePtr& topicName, const std::string& consumerName,
    const ConsumerSubResultPromisePtr& topicSubResultPromise) {
    std::shared_ptr<ConsumerImpl> consumer;
    ConsumerConfiguration config = conf_.clone();
    // Pause messageListener until all child topics are subscribed.
    // Otherwise messages may be acked before the parent consumer gets "Ready", causing ack failures.
    if (messageListener_) {
        config.setStartPaused(true);
    }
    auto client = client_.lock();
    if (!client) {
        topicSubResultPromise->setFailed(ResultAlreadyClosed);
        return;
    }
    ExecutorServicePtr internalListenerExecutor = client->getPartitionListenerExecutorProvider()->get();

    auto weakSelf = weak_from_this();
    config.setMessageListener([this, weakSelf](const Consumer& consumer, const Message& msg) {
        auto self = weakSelf.lock();
        if (self) {
            messageReceived(consumer, msg);
        }
    });

    int partitions = numPartitions == 0 ? 1 : numPartitions;

    // Apply total limit of receiver queue size across partitions
    config.setReceiverQueueSize(
        std::min(conf_.getReceiverQueueSize(),
                 (int)(conf_.getMaxTotalReceiverQueueSizeAcrossPartitions() / partitions)));

    Lock lock(mutex_);
    topicsPartitions_[topicName->toString()] = partitions;
    lock.unlock();
    numberTopicPartitions_->fetch_add(partitions);

    std::shared_ptr<std::atomic<int>> partitionsNeedCreate = std::make_shared<std::atomic<int>>(partitions);

    // non-partitioned topic
    if (numPartitions == 0) {
        // We don't have to add partition-n suffix
        try {
            consumer = std::make_shared<ConsumerImpl>(client, topicName->toString(), subscriptionName_,
                                                      config, topicName->isPersistent(), interceptors_,
                                                      internalListenerExecutor, true, NonPartitioned,
                                                      subscriptionMode_, startMessageId_);
        } catch (const std::runtime_error& e) {
            LOG_ERROR("Failed to create ConsumerImpl for " << topicName->toString() << ": " << e.what());
            topicSubResultPromise->setFailed(ResultConnectError);
            return;
        }
        consumer->getConsumerCreatedFuture().addListener(std::bind(
            &MultiTopicsConsumerImpl::handleSingleConsumerCreated, get_shared_this_ptr(),
            std::placeholders::_1, std::placeholders::_2, partitionsNeedCreate, topicSubResultPromise));
        consumers_.put(topicName->toString(), consumer);
        LOG_DEBUG("Creating Consumer for - " << topicName << " - " << consumerStr_);
        consumer->start();

    } else {
        std::vector<ConsumerImplPtr> consumers;
        for (int i = 0; i < numPartitions; i++) {
            std::string topicPartitionName = topicName->getTopicPartitionName(i);
            try {
                consumer = std::make_shared<ConsumerImpl>(client, topicPartitionName, subscriptionName_,
                                                          config, topicName->isPersistent(), interceptors_,
                                                          internalListenerExecutor, true, Partitioned,
                                                          subscriptionMode_, startMessageId_);
            } catch (const std::runtime_error& e) {
                LOG_ERROR("Failed to create ConsumerImpl for " << topicPartitionName << ": " << e.what());
                topicSubResultPromise->setFailed(ResultConnectError);
                return;
            }
            consumers.emplace_back(consumer);
        }
        for (size_t i = 0; i < consumers.size(); i++) {
            std::string topicPartitionName = topicName->getTopicPartitionName(i);
            auto&& consumer = consumers[i];
            consumer->getConsumerCreatedFuture().addListener(std::bind(
                &MultiTopicsConsumerImpl::handleSingleConsumerCreated, get_shared_this_ptr(),
                std::placeholders::_1, std::placeholders::_2, partitionsNeedCreate, topicSubResultPromise));
            consumer->setPartitionIndex(i);
            consumers_.put(topicPartitionName, consumer);
            LOG_DEBUG("Creating Consumer for - " << topicPartitionName << " - " << consumerStr_);
            consumer->start();
        }
    }
}

void MultiTopicsConsumerImpl::handleSingleConsumerCreated(
    Result result, const ConsumerImplBaseWeakPtr& consumerImplBaseWeakPtr,
    const std::shared_ptr<std::atomic<int>>& partitionsNeedCreate,
    const ConsumerSubResultPromisePtr& topicSubResultPromise) {
    if (state_ == Failed) {
        // one of the consumer creation failed, and we are cleaning up
        topicSubResultPromise->setFailed(ResultAlreadyClosed);
        LOG_ERROR("Unable to create Consumer " << consumerStr_ << " state == Failed, result: " << result);
        return;
    }

    int previous = partitionsNeedCreate->fetch_sub(1);
    assert(previous > 0);

    if (result != ResultOk) {
        topicSubResultPromise->setFailed(result);
        LOG_ERROR("Unable to create Consumer - " << consumerStr_ << " Error - " << result);
        return;
    }

    LOG_INFO("Successfully Subscribed to a single partition of topic in TopicsConsumer. "
             << "Partitions need to create : " << previous - 1);

    if (partitionsNeedCreate->load() == 0) {
        if (partitionsUpdateTimer_) {
            runPartitionUpdateTask();
        }
        topicSubResultPromise->setValue(Consumer(get_shared_this_ptr()));
    }
}

void MultiTopicsConsumerImpl::unsubscribeAsync(const ResultCallback& originalCallback) {
    LOG_INFO("[ Topics Consumer " << topic() << "," << subscriptionName_ << "] Unsubscribing");

    auto callback = [this, originalCallback](Result result) {
        if (result == ResultOk) {
            internalShutdown();
            LOG_INFO(getName() << "Unsubscribed successfully");
        } else {
            state_ = Ready;
            LOG_WARN(getName() << "Failed to unsubscribe: " << result);
        }
        if (originalCallback) {
            originalCallback(result);
        }
    };

    const auto state = state_.load();
    if (state == Closing || state == Closed) {
        callback(ResultAlreadyClosed);
        return;
    }
    state_ = Closing;

    auto self = get_shared_this_ptr();
    consumers_.forEachValue(
        [this, self, callback](const ConsumerImplPtr& consumer, const SharedFuture& future) {
            consumer->unsubscribeAsync([this, self, callback, future](Result result) {
                if (result != ResultOk) {
                    state_ = Failed;
                    LOG_ERROR("Error Closing one of the consumers in TopicsConsumer, result: "
                              << result << " subscription - " << subscriptionName_);
                }
                if (future.tryComplete()) {
                    LOG_DEBUG("Unsubscribed all of the partition consumer for TopicsConsumer.  - "
                              << consumerStr_);
                    callback((state_ != Failed) ? ResultOk : ResultUnknownError);
                }
            });
        },
        [callback] { callback(ResultOk); });
}

void MultiTopicsConsumerImpl::unsubscribeOneTopicAsync(const std::string& topic,
                                                       const ResultCallback& callback) {
    Lock lock(mutex_);
    std::map<std::string, int>::iterator it = topicsPartitions_.find(topic);
    if (it == topicsPartitions_.end()) {
        lock.unlock();
        LOG_ERROR("TopicsConsumer does not subscribe topic : " << topic << " subscription - "
                                                               << subscriptionName_);
        callback(ResultTopicNotFound);
        return;
    }
    int numberPartitions = it->second;
    lock.unlock();

    const auto state = state_.load();
    if (state == Closing || state == Closed) {
        LOG_ERROR("TopicsConsumer already closed when unsubscribe topic: " << topic << " subscription - "
                                                                           << subscriptionName_);
        callback(ResultAlreadyClosed);
        return;
    }

    TopicNamePtr topicName;
    if (!(topicName = TopicName::get(topic))) {
        LOG_ERROR("TopicName invalid: " << topic);
        callback(ResultUnknownError);
    }
    std::shared_ptr<std::atomic<int>> consumerUnsubed = std::make_shared<std::atomic<int>>(0);

    for (int i = 0; i < numberPartitions; i++) {
        std::string topicPartitionName = topicName->getTopicPartitionName(i);
        auto optConsumer = consumers_.find(topicPartitionName);
        if (!optConsumer) {
            LOG_ERROR("TopicsConsumer not subscribed on topicPartitionName: " << topicPartitionName);
            callback(ResultUnknownError);
            continue;
        }

        optConsumer.value()->unsubscribeAsync(
            std::bind(&MultiTopicsConsumerImpl::handleOneTopicUnsubscribedAsync, get_shared_this_ptr(),
                      std::placeholders::_1, consumerUnsubed, numberPartitions, topicName, topicPartitionName,
                      callback));
    }
}

void MultiTopicsConsumerImpl::handleOneTopicUnsubscribedAsync(
    Result result, const std::shared_ptr<std::atomic<int>>& consumerUnsubed, int numberPartitions,
    const TopicNamePtr& topicNamePtr, const std::string& topicPartitionName, const ResultCallback& callback) {
    (*consumerUnsubed)++;

    if (result != ResultOk) {
        state_ = Failed;
        LOG_ERROR("Error Closing one of the consumers in TopicsConsumer, result: "
                  << result << " topicPartitionName - " << topicPartitionName);
    }

    LOG_DEBUG("Successfully Unsubscribed one Consumer. topicPartitionName - " << topicPartitionName);

    auto optConsumer = consumers_.remove(topicPartitionName);
    if (optConsumer) {
        optConsumer.value()->pauseMessageListener();
    }

    if (consumerUnsubed->load() == numberPartitions) {
        LOG_DEBUG("Unsubscribed all of the partition consumer for TopicsConsumer.  - " << consumerStr_);
        std::map<std::string, int>::iterator it = topicsPartitions_.find(topicNamePtr->toString());
        if (it != topicsPartitions_.end()) {
            numberTopicPartitions_->fetch_sub(numberPartitions);
            Lock lock(mutex_);
            topicsPartitions_.erase(it);
            lock.unlock();
        }
        if (state_ != Failed) {
            callback(ResultOk);
        } else {
            callback(ResultUnknownError);
        }
        unAckedMessageTrackerPtr_->removeTopicMessage(topicNamePtr->toString());
        return;
    }
}

void MultiTopicsConsumerImpl::closeAsync(const ResultCallback& originalCallback) {
    std::weak_ptr<MultiTopicsConsumerImpl> weakSelf{get_shared_this_ptr()};
    auto callback = [weakSelf, originalCallback](Result result) {
        auto self = weakSelf.lock();
        if (self) {
            self->internalShutdown();
            if (result != ResultOk) {
                LOG_WARN(self->getName() << "Failed to close consumer: " << result);
                if (result != ResultAlreadyClosed) {
                    self->state_ = Failed;
                }
            }
        }
        if (originalCallback) {
            originalCallback(result);
        }
    };
    const auto state = state_.load();
    if (state == Closing || state == Closed) {
        callback(ResultOk);
        return;
    }

    state_ = Closing;

    cancelTimers();

    auto consumers = consumers_.move();
    *numberTopicPartitions_ = 0;
    if (consumers.empty()) {
        LOG_DEBUG("TopicsConsumer have no consumers to close "
                  << " topic" << topic() << " subscription - " << subscriptionName_);
        callback(ResultOk);
        return;
    }

    auto numConsumers = std::make_shared<std::atomic<size_t>>(consumers.size());
    for (auto&& kv : consumers) {
        auto& name = kv.first;
        auto& consumer = kv.second;
        consumer->closeAsync([name, numConsumers, callback](Result result) {
            const auto numConsumersLeft = --*numConsumers;
            LOG_DEBUG("Closing the consumer for partition - " << name << " numConsumersLeft - "
                                                              << numConsumersLeft);

            if (result != ResultOk) {
                LOG_ERROR("Closing the consumer failed for partition - " << name << " with error - "
                                                                         << result);
            }
            if (numConsumersLeft == 0) {
                callback(result);
            }
        });
    }

    // fail pending receive
    failPendingReceiveCallback();
    failPendingBatchReceiveCallback();

    // cancel timer
    batchReceiveTimer_->cancel();
}

void MultiTopicsConsumerImpl::messageReceived(const Consumer& consumer, const Message& msg) {
    if (PULSAR_UNLIKELY(duringSeek_.load(std::memory_order_acquire))) {
        return;
    }
    LOG_DEBUG("Received Message from one of the topic - " << consumer.getTopic()
                                                          << " message:" << msg.getDataAsString());
    msg.impl_->setTopicName(consumer.impl_->getTopicPtr());
    msg.impl_->consumerPtr_ = std::static_pointer_cast<ConsumerImpl>(consumer.impl_);

    Lock lock(pendingReceiveMutex_);
    if (!pendingReceives_.empty()) {
        ReceiveCallback callback = pendingReceives_.front();
        pendingReceives_.pop();
        lock.unlock();
        auto weakSelf = weak_from_this();
        listenerExecutor_->postWork([this, weakSelf, msg, callback]() {
            auto self = weakSelf.lock();
            if (self) {
                notifyPendingReceivedCallback(ResultOk, msg, callback);
                auto consumer = msg.impl_->consumerPtr_.lock();
                if (consumer) {
                    consumer->increaseAvailablePermits(msg);
                }
            }
        });
        return;
    }

    incomingMessages_.push(msg);
    incomingMessagesSize_.fetch_add(msg.getLength());

    // try trigger pending batch messages
    Lock batchOptionLock(batchReceiveOptionMutex_);
    if (hasEnoughMessagesForBatchReceive()) {
        ConsumerImplBase::notifyBatchPendingReceivedCallback();
    }
    batchOptionLock.unlock();

    if (messageListener_) {
        listenerExecutor_->postWork(
            std::bind(&MultiTopicsConsumerImpl::internalListener, get_shared_this_ptr(), consumer));
    }
}

void MultiTopicsConsumerImpl::internalListener(const Consumer& consumer) {
    Message m;
    incomingMessages_.pop(m);
    try {
        Consumer self{get_shared_this_ptr()};
        messageProcessed(m);
        messageListener_(self, m);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception thrown from listener of Partitioned Consumer" << e.what());
    }
}

Result MultiTopicsConsumerImpl::receive(Message& msg) {
    if (state_ != Ready) {
        return ResultAlreadyClosed;
    }

    if (messageListener_) {
        LOG_ERROR("Can not receive when a listener has been set");
        return ResultInvalidConfiguration;
    }
    incomingMessages_.pop(msg);
    messageProcessed(msg);

    return ResultOk;
}

Result MultiTopicsConsumerImpl::receive(Message& msg, int timeout) {
    if (state_ != Ready) {
        return ResultAlreadyClosed;
    }

    if (messageListener_) {
        LOG_ERROR("Can not receive when a listener has been set");
        return ResultInvalidConfiguration;
    }

    if (incomingMessages_.pop(msg, std::chrono::milliseconds(timeout))) {
        messageProcessed(msg);
        return ResultOk;
    } else {
        if (state_ != Ready) {
            return ResultAlreadyClosed;
        }
        return ResultTimeout;
    }
}

void MultiTopicsConsumerImpl::receiveAsync(const ReceiveCallback& callback) {
    Message msg;

    // fail the callback if consumer is closing or closed
    if (state_ != Ready) {
        callback(ResultAlreadyClosed, msg);
        return;
    }

    Lock lock(pendingReceiveMutex_);
    if (incomingMessages_.pop(msg, std::chrono::milliseconds(0))) {
        lock.unlock();
        messageProcessed(msg);
        callback(ResultOk, msg);
    } else {
        pendingReceives_.push(callback);
    }
}

void MultiTopicsConsumerImpl::failPendingReceiveCallback() {
    Message msg;

    incomingMessages_.close();

    Lock lock(pendingReceiveMutex_);
    while (!pendingReceives_.empty()) {
        ReceiveCallback callback = pendingReceives_.front();
        pendingReceives_.pop();
        auto weakSelf = weak_from_this();
        listenerExecutor_->postWork([this, weakSelf, msg, callback]() {
            auto self = weakSelf.lock();
            if (self) {
                notifyPendingReceivedCallback(ResultAlreadyClosed, msg, callback);
            }
        });
    }
    lock.unlock();
}

void MultiTopicsConsumerImpl::notifyPendingReceivedCallback(Result result, const Message& msg,
                                                            const ReceiveCallback& callback) {
    if (result == ResultOk) {
        unAckedMessageTrackerPtr_->add(msg.getMessageId());
    }
    callback(result, msg);
}

static void logErrorTopicNameForAcknowledge(const std::string& topic) {
    if (topic.empty()) {
        LOG_ERROR("MessageId without a topic name cannot be acknowledged for a multi-topics consumer");
    } else {
        LOG_ERROR("Message of topic: " << topic << " not in consumers");
    }
}

void MultiTopicsConsumerImpl::acknowledgeAsync(const MessageId& msgId, const ResultCallback& callback) {
    if (state_ != Ready) {
        interceptors_->onAcknowledge(Consumer(shared_from_this()), ResultAlreadyClosed, msgId);
        callback(ResultAlreadyClosed);
        return;
    }

    const std::string& topicPartitionName = msgId.getTopicName();
    auto optConsumer = consumers_.find(topicPartitionName);

    if (optConsumer) {
        unAckedMessageTrackerPtr_->remove(msgId);
        optConsumer.value()->acknowledgeAsync(msgId, callback);
    } else {
        logErrorTopicNameForAcknowledge(topicPartitionName);
        callback(ResultOperationNotSupported);
    }
}

void MultiTopicsConsumerImpl::acknowledgeAsync(const MessageIdList& messageIdList,
                                               const ResultCallback& callback) {
    if (state_ != Ready) {
        callback(ResultAlreadyClosed);
        return;
    }

    std::unordered_map<std::string, MessageIdList> topicToMessageId;
    for (const MessageId& messageId : messageIdList) {
        const auto& topicName = messageId.getTopicName();
        if (topicName.empty()) {
            logErrorTopicNameForAcknowledge(topicName);
            callback(ResultOperationNotSupported);
            return;
        }
        topicToMessageId[topicName].emplace_back(messageId);
    }

    auto needCallBack = std::make_shared<std::atomic<int>>(topicToMessageId.size());
    auto cb = [callback, needCallBack](Result result) {
        if (result != ResultOk) {
            LOG_ERROR("Filed when acknowledge list: " << result);
            // set needCallBack is -1 to avoid repeated callback.
            needCallBack->store(-1);
            callback(result);
            return;
        }
        if (--(*needCallBack) == 0) {
            callback(result);
        }
    };
    for (const auto& kv : topicToMessageId) {
        auto optConsumer = consumers_.find(kv.first);
        if (optConsumer) {
            unAckedMessageTrackerPtr_->remove(kv.second);
            optConsumer.value()->acknowledgeAsync(kv.second, cb);
        } else {
            logErrorTopicNameForAcknowledge(kv.first);
            callback(ResultOperationNotSupported);
        }
    }
}

void MultiTopicsConsumerImpl::acknowledgeCumulativeAsync(const MessageId& msgId,
                                                         const ResultCallback& callback) {
    const auto& topic = msgId.getTopicName();
    auto optConsumer = consumers_.find(msgId.getTopicName());
    if (optConsumer) {
        unAckedMessageTrackerPtr_->removeMessagesTill(msgId);
        optConsumer.value()->acknowledgeCumulativeAsync(msgId, callback);
    } else {
        logErrorTopicNameForAcknowledge(topic);
        callback(ResultOperationNotSupported);
    }
}

void MultiTopicsConsumerImpl::negativeAcknowledge(const MessageId& msgId) {
    auto optConsumer = consumers_.find(msgId.getTopicName());

    if (optConsumer) {
        unAckedMessageTrackerPtr_->remove(msgId);
        optConsumer.value()->negativeAcknowledge(msgId);
    }
}

MultiTopicsConsumerImpl::~MultiTopicsConsumerImpl() { internalShutdown(); }

Future<Result, ConsumerImplBaseWeakPtr> MultiTopicsConsumerImpl::getConsumerCreatedFuture() {
    return multiTopicsConsumerCreatedPromise_.getFuture();
}
const std::string& MultiTopicsConsumerImpl::getSubscriptionName() const { return subscriptionName_; }

const std::string& MultiTopicsConsumerImpl::getTopic() const { return topic(); }

const std::string& MultiTopicsConsumerImpl::getName() const { return consumerStr_; }

void MultiTopicsConsumerImpl::shutdown() { internalShutdown(); }

void MultiTopicsConsumerImpl::internalShutdown() {
    cancelTimers();
    incomingMessages_.clear();
    topicsPartitions_.clear();
    unAckedMessageTrackerPtr_->clear();
    interceptors_->close();
    auto client = client_.lock();
    if (client) {
        client->cleanupConsumer(this);
    }
    consumers_.clear();
    topicsPartitions_.clear();
    if (failedResult != ResultOk) {
        multiTopicsConsumerCreatedPromise_.setFailed(failedResult);
    } else {
        multiTopicsConsumerCreatedPromise_.setFailed(ResultAlreadyClosed);
    }
    state_ = Closed;
}

bool MultiTopicsConsumerImpl::isClosed() { return state_ == Closed; }

bool MultiTopicsConsumerImpl::isOpen() { return state_ == Ready; }

void MultiTopicsConsumerImpl::receiveMessages() {
    const auto receiverQueueSize = conf_.getReceiverQueueSize();
    consumers_.forEachValue([receiverQueueSize](const ConsumerImplPtr& consumer) {
        consumer->sendFlowPermitsToBroker(consumer->getCnx().lock(), receiverQueueSize);
        LOG_DEBUG("Sending FLOW command for consumer - " << consumer->getConsumerId());
    });
}

Result MultiTopicsConsumerImpl::pauseMessageListener() {
    if (!messageListener_) {
        return ResultInvalidConfiguration;
    }
    consumers_.forEachValue([](const ConsumerImplPtr& consumer) { consumer->pauseMessageListener(); });
    return ResultOk;
}

Result MultiTopicsConsumerImpl::resumeMessageListener() {
    if (!messageListener_) {
        return ResultInvalidConfiguration;
    }
    consumers_.forEachValue([](const ConsumerImplPtr& consumer) { consumer->resumeMessageListener(); });
    return ResultOk;
}

void MultiTopicsConsumerImpl::redeliverUnacknowledgedMessages() {
    LOG_DEBUG("Sending RedeliverUnacknowledgedMessages command for partitioned consumer.");
    consumers_.forEachValue(
        [](const ConsumerImplPtr& consumer) { consumer->redeliverUnacknowledgedMessages(); });
    unAckedMessageTrackerPtr_->clear();
}

void MultiTopicsConsumerImpl::redeliverUnacknowledgedMessages(const std::set<MessageId>& messageIds) {
    if (messageIds.empty()) {
        return;
    }
    if (conf_.getConsumerType() != ConsumerShared && conf_.getConsumerType() != ConsumerKeyShared) {
        redeliverUnacknowledgedMessages();
        return;
    }

    LOG_DEBUG("Sending RedeliverUnacknowledgedMessages command for partitioned consumer.");
    std::unordered_map<std::string, std::set<MessageId>> topicToMessageId;
    for (const MessageId& messageId : messageIds) {
        const auto& topicName = messageId.getTopicName();
        topicToMessageId[topicName].emplace(messageId);
    }

    for (const auto& kv : topicToMessageId) {
        auto optConsumer = consumers_.find(kv.first);
        if (optConsumer) {
            optConsumer.value()->redeliverUnacknowledgedMessages(kv.second);
        } else {
            LOG_ERROR("Message of topic: " << kv.first << " not in consumers");
        }
    }
}

int MultiTopicsConsumerImpl::getNumOfPrefetchedMessages() const { return incomingMessages_.size(); }

void MultiTopicsConsumerImpl::getBrokerConsumerStatsAsync(const BrokerConsumerStatsCallback& callback) {
    if (state_ != Ready) {
        callback(ResultConsumerNotInitialized, BrokerConsumerStats());
        return;
    }
    Lock lock(mutex_);
    MultiTopicsBrokerConsumerStatsPtr statsPtr =
        std::make_shared<MultiTopicsBrokerConsumerStatsImpl>(numberTopicPartitions_->load());
    LatchPtr latchPtr = std::make_shared<Latch>(numberTopicPartitions_->load());
    lock.unlock();

    size_t i = 0;
    consumers_.forEachValue([this, &latchPtr, &statsPtr, &i, callback](const ConsumerImplPtr& consumer) {
        size_t index = i++;
        auto weakSelf = weak_from_this();
        consumer->getBrokerConsumerStatsAsync([this, weakSelf, latchPtr, statsPtr, index, callback](
                                                  Result result, const BrokerConsumerStats& stats) {
            auto self = weakSelf.lock();
            if (self) {
                handleGetConsumerStats(result, stats, latchPtr, statsPtr, index, callback);
            }
        });
    });
}

void MultiTopicsConsumerImpl::getLastMessageIdAsync(const BrokerGetLastMessageIdCallback& callback) {
    callback(ResultOperationNotSupported, GetLastMessageIdResponse());
}

void MultiTopicsConsumerImpl::handleGetConsumerStats(Result res,
                                                     const BrokerConsumerStats& brokerConsumerStats,
                                                     const LatchPtr& latchPtr,
                                                     const MultiTopicsBrokerConsumerStatsPtr& statsPtr,
                                                     size_t index,
                                                     const BrokerConsumerStatsCallback& callback) {
    Lock lock(mutex_);
    if (res == ResultOk) {
        latchPtr->countdown();
        statsPtr->add(brokerConsumerStats, index);
    } else {
        lock.unlock();
        callback(res, BrokerConsumerStats());
        return;
    }
    if (latchPtr->getCount() == 0) {
        lock.unlock();
        callback(ResultOk, BrokerConsumerStats(statsPtr));
    }
}

std::shared_ptr<TopicName> MultiTopicsConsumerImpl::topicNamesValid(const std::vector<std::string>& topics) {
    TopicNamePtr topicNamePtr = std::shared_ptr<TopicName>();

    // all topics name valid, and all topics have same namespace
    for (std::vector<std::string>::const_iterator itr = topics.begin(); itr != topics.end(); itr++) {
        // topic name valid
        if (!(topicNamePtr = TopicName::get(*itr))) {
            LOG_ERROR("Topic name invalid when init " << *itr);
            return std::shared_ptr<TopicName>();
        }
    }

    return topicNamePtr;
}

void MultiTopicsConsumerImpl::beforeSeek() {
    duringSeek_.store(true, std::memory_order_release);
    consumers_.forEachValue([](const ConsumerImplPtr& consumer) { consumer->pauseMessageListener(); });
    unAckedMessageTrackerPtr_->clear();
    incomingMessages_.clear();
    incomingMessagesSize_ = 0L;
}

void MultiTopicsConsumerImpl::afterSeek() {
    duringSeek_.store(false, std::memory_order_release);
    auto self = get_shared_this_ptr();
    listenerExecutor_->postWork([this, self] {
        consumers_.forEachValue([](const ConsumerImplPtr& consumer) { consumer->resumeMessageListener(); });
    });
}

void MultiTopicsConsumerImpl::seekAsync(const MessageId& msgId, const ResultCallback& callback) {
    if (msgId == MessageId::earliest() || msgId == MessageId::latest()) {
        return seekAllAsync(msgId, callback);
    }

    auto optConsumer = consumers_.find(msgId.getTopicName());
    if (!optConsumer) {
        LOG_ERROR(getName() << "cannot seek a message id whose topic \"" + msgId.getTopicName() +
                                   "\" is not subscribed");
        callback(ResultOperationNotSupported);
        return;
    }

    beforeSeek();
    auto weakSelf = weak_from_this();
    optConsumer.get()->seekAsync(msgId, [this, weakSelf, callback](Result result) {
        auto self = weakSelf.lock();
        if (self) {
            afterSeek();
            callback(result);
        } else {
            callback(ResultAlreadyClosed);
        }
    });
}

void MultiTopicsConsumerImpl::seekAsync(uint64_t timestamp, const ResultCallback& callback) {
    seekAllAsync(timestamp, callback);
}

void MultiTopicsConsumerImpl::setNegativeAcknowledgeEnabledForTesting(bool enabled) {
    consumers_.forEachValue([enabled](const ConsumerImplPtr& consumer) {
        consumer->setNegativeAcknowledgeEnabledForTesting(enabled);
    });
}

bool MultiTopicsConsumerImpl::isConnected() const {
    if (state_ != Ready) {
        return false;
    }

    return !consumers_.findFirstValueIf(
        [](const ConsumerImplPtr& consumer) { return !consumer->isConnected(); });
}

uint64_t MultiTopicsConsumerImpl::getNumberOfConnectedConsumer() {
    uint64_t numberOfConnectedConsumer = 0;
    consumers_.forEachValue([&numberOfConnectedConsumer](const ConsumerImplPtr& consumer) {
        if (consumer->isConnected()) {
            numberOfConnectedConsumer++;
        }
    });
    return numberOfConnectedConsumer;
}
void MultiTopicsConsumerImpl::runPartitionUpdateTask() {
    partitionsUpdateTimer_->expires_from_now(partitionsUpdateInterval_);
    auto weakSelf = weak_from_this();
    partitionsUpdateTimer_->async_wait([weakSelf](const ASIO_ERROR& ec) {
        // If two requests call runPartitionUpdateTask at the same time, the timer will fail, and it
        // cannot continue at this time, and the request needs to be ignored.
        auto self = weakSelf.lock();
        if (self && !ec) {
            self->topicPartitionUpdate();
        }
    });
}
void MultiTopicsConsumerImpl::topicPartitionUpdate() {
    using namespace std::placeholders;
    Lock lock(mutex_);
    auto topicsPartitions = topicsPartitions_;
    lock.unlock();
    for (const auto& item : topicsPartitions) {
        auto topicName = TopicName::get(item.first);
        auto currentNumPartitions = item.second;
        auto weakSelf = weak_from_this();
        lookupServicePtr_->getPartitionMetadataAsync(topicName).addListener(
            [this, weakSelf, topicName, currentNumPartitions](Result result,
                                                              const LookupDataResultPtr& lookupDataResult) {
                auto self = weakSelf.lock();
                if (self) {
                    this->handleGetPartitions(topicName, result, lookupDataResult, currentNumPartitions);
                }
            });
    }
}
void MultiTopicsConsumerImpl::handleGetPartitions(const TopicNamePtr& topicName, Result result,
                                                  const LookupDataResultPtr& lookupDataResult,
                                                  int currentNumPartitions) {
    if (state_ != Ready) {
        return;
    }
    if (!result) {
        const auto newNumPartitions = static_cast<unsigned int>(lookupDataResult->getPartitions());
        if (newNumPartitions > currentNumPartitions) {
            LOG_INFO("new partition count: " << newNumPartitions
                                             << " current partition count: " << currentNumPartitions);
            auto partitionsNeedCreate =
                std::make_shared<std::atomic<int>>(newNumPartitions - currentNumPartitions);
            ConsumerSubResultPromisePtr topicPromise = std::make_shared<Promise<Result, Consumer>>();
            Lock lock(mutex_);
            topicsPartitions_[topicName->toString()] = newNumPartitions;
            lock.unlock();
            numberTopicPartitions_->fetch_add(newNumPartitions - currentNumPartitions);
            for (unsigned int i = currentNumPartitions; i < newNumPartitions; i++) {
                subscribeSingleNewConsumer(newNumPartitions, topicName, i, topicPromise,
                                           partitionsNeedCreate);
            }
            // `runPartitionUpdateTask()` will be called in `handleSingleConsumerCreated()`
            return;
        }
    } else {
        LOG_WARN("Failed to getPartitionMetadata: " << strResult(result));
    }
    runPartitionUpdateTask();
}

void MultiTopicsConsumerImpl::subscribeSingleNewConsumer(
    int numPartitions, const TopicNamePtr& topicName, int partitionIndex,
    const ConsumerSubResultPromisePtr& topicSubResultPromise,
    const std::shared_ptr<std::atomic<int>>& partitionsNeedCreate) {
    ConsumerConfiguration config = conf_.clone();
    auto client = client_.lock();
    if (!client) {
        topicSubResultPromise->setFailed(ResultAlreadyClosed);
        return;
    }
    ExecutorServicePtr internalListenerExecutor = client->getPartitionListenerExecutorProvider()->get();
    auto weakSelf = weak_from_this();
    config.setMessageListener([this, weakSelf](const Consumer& consumer, const Message& msg) {
        auto self = weakSelf.lock();
        if (self) {
            messageReceived(consumer, msg);
        }
    });

    // Apply total limit of receiver queue size across partitions
    config.setReceiverQueueSize(
        std::min(conf_.getReceiverQueueSize(),
                 (int)(conf_.getMaxTotalReceiverQueueSizeAcrossPartitions() / numPartitions)));

    std::string topicPartitionName = topicName->getTopicPartitionName(partitionIndex);

    auto consumer = std::make_shared<ConsumerImpl>(
        client, topicPartitionName, subscriptionName_, config, topicName->isPersistent(), interceptors_,
        internalListenerExecutor, true, Partitioned, subscriptionMode_, startMessageId_);
    consumer->getConsumerCreatedFuture().addListener(
        [this, weakSelf, partitionsNeedCreate, topicSubResultPromise](
            Result result, const ConsumerImplBaseWeakPtr& consumerImplBaseWeakPtr) {
            auto self = weakSelf.lock();
            if (self) {
                handleSingleConsumerCreated(result, consumerImplBaseWeakPtr, partitionsNeedCreate,
                                            topicSubResultPromise);
            }
        });
    consumer->setPartitionIndex(partitionIndex);
    consumer->start();
    consumers_.put(topicPartitionName, consumer);
    LOG_INFO("Add Creating Consumer for - " << topicPartitionName << " - " << consumerStr_
                                            << " consumerSize: " << consumers_.size());
}

bool MultiTopicsConsumerImpl::hasEnoughMessagesForBatchReceive() const {
    if (batchReceivePolicy_.getMaxNumMessages() <= 0 && batchReceivePolicy_.getMaxNumBytes() <= 0) {
        return false;
    }
    return (batchReceivePolicy_.getMaxNumMessages() > 0 &&
            incomingMessages_.size() >= batchReceivePolicy_.getMaxNumMessages()) ||
           (batchReceivePolicy_.getMaxNumBytes() > 0 &&
            incomingMessagesSize_ >= batchReceivePolicy_.getMaxNumBytes());
}

void MultiTopicsConsumerImpl::notifyBatchPendingReceivedCallback(const BatchReceiveCallback& callback) {
    auto messages = std::make_shared<MessagesImpl>(batchReceivePolicy_.getMaxNumMessages(),
                                                   batchReceivePolicy_.getMaxNumBytes());
    Message msg;
    while (incomingMessages_.popIf(
        msg, [&messages](const Message& peekMsg) { return messages->canAdd(peekMsg); })) {
        messageProcessed(msg);
        messages->add(msg);
    }
    auto weakSelf = weak_from_this();
    listenerExecutor_->postWork([weakSelf, callback, messages]() {
        auto self = weakSelf.lock();
        if (self) {
            callback(ResultOk, messages->getMessageList());
        }
    });
}

void MultiTopicsConsumerImpl::messageProcessed(Message& msg) {
    incomingMessagesSize_.fetch_sub(msg.getLength());
    unAckedMessageTrackerPtr_->add(msg.getMessageId());
    auto consumer = msg.impl_->consumerPtr_.lock();
    if (consumer) {
        consumer->increaseAvailablePermits(msg);
    }
}

std::shared_ptr<MultiTopicsConsumerImpl> MultiTopicsConsumerImpl::get_shared_this_ptr() {
    return std::dynamic_pointer_cast<MultiTopicsConsumerImpl>(shared_from_this());
}

void MultiTopicsConsumerImpl::beforeConnectionChange(ClientConnection& cnx) {
    throw std::runtime_error("The connection_ field should not be modified for a MultiTopicsConsumerImpl");
}

void MultiTopicsConsumerImpl::cancelTimers() noexcept {
    if (partitionsUpdateTimer_) {
        ASIO_ERROR ec;
        partitionsUpdateTimer_->cancel(ec);
    }
}

void MultiTopicsConsumerImpl::hasMessageAvailableAsync(const HasMessageAvailableCallback& callback) {
    if (incomingMessagesSize_ > 0) {
        callback(ResultOk, true);
        return;
    }

    auto hasMessageAvailable = std::make_shared<std::atomic<bool>>();
    auto needCallBack = std::make_shared<std::atomic<int>>(consumers_.size());
    auto self = get_shared_this_ptr();

    consumers_.forEachValue(
        [self, needCallBack, callback, hasMessageAvailable](const ConsumerImplPtr& consumer) {
            consumer->hasMessageAvailableAsync(
                [self, needCallBack, callback, hasMessageAvailable](Result result, bool hasMsg) {
                    if (result != ResultOk) {
                        LOG_ERROR("Filed when acknowledge list: " << result);
                        // set needCallBack is -1 to avoid repeated callback.
                        needCallBack->store(-1);
                        callback(result, false);
                        return;
                    }

                    if (hasMsg) {
                        hasMessageAvailable->store(hasMsg);
                    }

                    if (--(*needCallBack) == 0) {
                        callback(result, hasMessageAvailable->load() || self->incomingMessagesSize_ > 0);
                    }
                });
        });
}

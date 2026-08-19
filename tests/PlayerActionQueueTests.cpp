#include "game/PlayerActions.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

class TestRun final {
  public:
    bool expect(bool condition, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return true;
        ++mFailures;
        std::cerr << "FAIL: " << contract << '\n';
        return false;
    }

    int result() const {
        if (mFailures == 0)
            std::cout << mAssertions << " assertions passed\n";
        return mFailures == 0 ? 0 : 1;
    }

  private:
    int mAssertions = 0;
    int mFailures = 0;
};

kue::PlayerActionRequest request(int clientId) {
    return {kue::PlayerAction::Kill,
            kue::PlayerTargetPayload{static_cast<std::uint64_t>(clientId)}};
}

std::uint64_t targetClientId(const kue::PlayerActionRequest& request) {
    const auto* payload = std::get_if<kue::PlayerTargetPayload>(&request.payload);
    return payload ? payload->clientId : UINT64_MAX;
}

void testCapacityAndOrder(TestRun& run) {
    kue::PlayerActionQueue queue;
    for (std::size_t index = 0; index < kue::PlayerActionQueue::kCapacity; ++index) {
        run.expect(queue.push(request(static_cast<int>(index))) ==
                       kue::PlayerActionEnqueueResult::Queued,
                   "every declared-capacity entry is accepted");
    }
    run.expect(queue.push(request(64)) == kue::PlayerActionEnqueueResult::CapacityExceeded,
               "the first over-capacity entry is rejected");

    for (std::size_t index = 0; index < kue::PlayerActionQueue::kCapacity; ++index) {
        kue::PlayerActionRequest actual;
        run.expect(queue.pop(actual), "every accepted entry can be removed");
        run.expect(actual.action == kue::PlayerAction::Kill,
                   "removed entries preserve their action");
        run.expect(targetClientId(actual) == index, "removed entries preserve FIFO client order");
    }
    kue::PlayerActionRequest actual;
    run.expect(!queue.pop(actual), "an emptied queue rejects removal");
}

void testWraparound(TestRun& run) {
    kue::PlayerActionQueue queue;
    constexpr std::size_t firstBatch = kue::PlayerActionQueue::kCapacity - 3;
    for (std::size_t index = 0; index < firstBatch; ++index)
        run.expect(queue.push(request(static_cast<int>(index))) ==
                       kue::PlayerActionEnqueueResult::Queued,
                   "initial entries are accepted");

    for (std::size_t index = 0; index < firstBatch; ++index) {
        kue::PlayerActionRequest actual;
        run.expect(queue.pop(actual), "initial entries are removed");
        run.expect(targetClientId(actual) == index, "initial entries remain ordered");
    }

    for (std::size_t index = 0; index < kue::PlayerActionQueue::kCapacity; ++index) {
        const int clientId = static_cast<int>(1000 + index);
        run.expect(queue.push(request(clientId)) == kue::PlayerActionEnqueueResult::Queued,
                   "wrapped entries are accepted to full capacity");
    }
    run.expect(queue.push(request(2000)) == kue::PlayerActionEnqueueResult::CapacityExceeded,
               "wrapped full queue rejects overflow");

    for (std::size_t index = 0; index < kue::PlayerActionQueue::kCapacity; ++index) {
        kue::PlayerActionRequest actual;
        run.expect(queue.pop(actual), "wrapped entries are removed");
        run.expect(targetClientId(actual) == 1000 + index, "wrapped entries remain FIFO ordered");
    }
}

void testValidation(TestRun& run) {
    kue::PlayerActionQueue queue;
    run.expect(queue.push({kue::PlayerAction::None, kue::NoPlayerActionPayload{}}) ==
                   kue::PlayerActionEnqueueResult::InvalidRequest,
               "an unsupported action is rejected before enqueue");
    run.expect(queue.push({kue::PlayerAction::Kill, kue::NoPlayerActionPayload{}}) ==
                   kue::PlayerActionEnqueueResult::InvalidRequest,
               "a mismatched payload is rejected before enqueue");
    run.expect(queue.push({kue::PlayerAction::SpawnEnemy,
                           kue::EnemySpawnPayload{{{7U}, 3U}, 0U, kue::EnemySpawnArea::Inside}}) ==
                   kue::PlayerActionEnqueueResult::InvalidRequest,
               "an invalid typed payload is rejected before enqueue");
    kue::PlayerActionRequest output = request(91);
    run.expect(!queue.pop(output), "rejected requests do not consume queue capacity");
    run.expect(targetClientId(output) == 91U, "empty removal preserves its output request");
}

void testPendingActionHandoff(TestRun& run) {
    kue::PlayerActionQueue queue;
    for (std::size_t index = 0; index < kue::PlayerActionQueue::kCapacity; ++index) {
        run.expect(queue.push(request(static_cast<int>(index))) ==
                       kue::PlayerActionEnqueueResult::Queued,
                   "handoff fixture fills the bounded queue");
    }

    kue::PendingPlayerAction pending;
    run.expect(pending.capture(request(9000)) == kue::PlayerActionCaptureResult::Captured,
               "the handoff captures one menu action");
    run.expect(pending.capture(request(9001)) == kue::PlayerActionCaptureResult::Occupied,
               "the handoff refuses a second menu action");

    kue::PlayerActionRequest attempt;
    run.expect(pending.read(attempt), "the captured action is available for enqueue");
    kue::PlayerActionEnqueueResult result = queue.push(attempt);
    run.expect(result == kue::PlayerActionEnqueueResult::CapacityExceeded,
               "the saturated queue reports capacity rejection");
    pending.applyEnqueueResult(result);

    kue::PlayerActionRequest retained;
    run.expect(pending.read(retained), "capacity rejection retains the menu action");
    run.expect(targetClientId(retained) == 9000U,
               "capacity rejection retains the original action exactly");

    kue::PlayerActionRequest removed;
    run.expect(queue.pop(removed), "one queued action can make capacity available");
    run.expect(targetClientId(removed) == 0U, "capacity release preserves FIFO order");
    run.expect(pending.read(attempt), "the retained action is available for one retry");
    result = queue.push(attempt);
    run.expect(result == kue::PlayerActionEnqueueResult::Queued,
               "the first available slot accepts the retained action");
    pending.applyEnqueueResult(result);
    run.expect(!pending.read(attempt), "successful enqueue clears the pending action");

    std::size_t retainedDeliveries = 0;
    std::size_t rejectedDeliveries = 0;
    for (std::size_t index = 0; index < kue::PlayerActionQueue::kCapacity; ++index) {
        run.expect(queue.pop(removed), "the handoff fixture drains every queued action");
        retainedDeliveries += targetClientId(removed) == 9000U ? 1U : 0U;
        rejectedDeliveries += targetClientId(removed) == 9001U ? 1U : 0U;
    }
    run.expect(retainedDeliveries == 1U, "the retained action is delivered exactly once");
    run.expect(rejectedDeliveries == 0U, "the refused second action is never delivered");

    run.expect(pending.capture({kue::PlayerAction::None, kue::NoPlayerActionPayload{}}) ==
                   kue::PlayerActionCaptureResult::InvalidRequest,
               "invalid menu actions fail before occupying the handoff");
    run.expect(!pending.read(attempt), "invalid capture leaves the handoff empty");
    run.expect(pending.capture(request(9002)) == kue::PlayerActionCaptureResult::Captured,
               "a valid action can occupy the cleared handoff");
    pending.applyEnqueueResult(kue::PlayerActionEnqueueResult::InvalidRequest);
    run.expect(!pending.read(attempt),
               "an invalid enqueue result clears the programming-error request");
}

void testCatalogIdentityHandoff(TestRun& run) {
    constexpr kue::EnemyTypeId selected{{41U}, 255U};
    const kue::PlayerActionRequest request{
        kue::PlayerAction::SpawnEnemyAtPlayer,
        kue::EnemyAtPlayerPayload{selected, 20U, kue::EnemySpawnArea::Outside, 4095U}};
    kue::PendingPlayerAction pending;
    run.expect(pending.capture(request) == kue::PlayerActionCaptureResult::Captured,
               "a generation-bearing enemy selection enters the pending handoff");
    kue::PlayerActionRequest captured;
    run.expect(pending.read(captured), "the generation-bearing selection leaves the handoff");
    kue::PlayerActionQueue queue;
    const kue::PlayerActionEnqueueResult result = queue.push(captured);
    run.expect(result == kue::PlayerActionEnqueueResult::Queued,
               "the generation-bearing selection enters the bounded queue");
    pending.applyEnqueueResult(result);
    kue::PlayerActionRequest delivered;
    run.expect(queue.pop(delivered), "the generation-bearing selection leaves the queue");
    const auto* payload = std::get_if<kue::EnemyAtPlayerPayload>(&delivered.payload);
    run.expect(payload && payload->enemyType == selected,
               "the queue preserves the exact catalog generation and index");
}

void testConcurrentProducers(TestRun& run) {
    constexpr std::size_t producerCount = 4;
    constexpr std::size_t requestsPerProducer = kue::PlayerActionQueue::kCapacity / producerCount;
    kue::PlayerActionQueue queue;
    std::array<std::thread, producerCount> producers;
    std::array<std::array<bool, requestsPerProducer>, producerCount> accepted{};
    for (std::size_t producer = 0; producer < producers.size(); ++producer) {
        producers[producer] = std::thread([producer, &queue, &accepted] {
            for (std::size_t index = 0; index < requestsPerProducer; ++index) {
                const int clientId = static_cast<int>(producer * requestsPerProducer + index);
                accepted[producer][index] =
                    queue.push(request(clientId)) == kue::PlayerActionEnqueueResult::Queued;
            }
        });
    }
    for (std::thread& producer : producers)
        producer.join();
    for (const auto& producer : accepted)
        for (const bool requestAccepted : producer)
            run.expect(requestAccepted, "concurrent producers fill the declared capacity exactly");

    std::array<bool, kue::PlayerActionQueue::kCapacity> observed{};
    for (std::size_t index = 0; index < observed.size(); ++index) {
        kue::PlayerActionRequest actual;
        run.expect(queue.pop(actual), "every concurrently accepted request can be removed");
        const std::uint64_t clientId = targetClientId(actual);
        run.expect(clientId < observed.size(), "concurrent requests preserve a valid client ID");
        if (clientId < observed.size()) {
            run.expect(!observed[static_cast<std::size_t>(clientId)],
                       "concurrent requests are not duplicated");
            observed[static_cast<std::size_t>(clientId)] = true;
        }
    }
    for (const bool clientObserved : observed)
        run.expect(clientObserved, "concurrent producers lose no accepted request");
}

}

int main() {
    TestRun run;
    testCapacityAndOrder(run);
    testWraparound(run);
    testValidation(run);
    testPendingActionHandoff(run);
    testCatalogIdentityHandoff(run);
    testConcurrentProducers(run);
    return run.result();
}

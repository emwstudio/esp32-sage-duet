#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace esp_now_link {

enum class MessageType : uint8_t {
    Hello = 1,
    Prepare = 2,
    Ready = 3,
    Play = 4,
    Done = 5,
    Stop = 6,
    Ack = 7,
};

struct __attribute__((packed)) Message {
    uint16_t magic;
    uint8_t version;
    MessageType type;
    uint32_t sequence;
    char session_id[33];
    uint32_t turn;
    uint8_t text_length;
    char text[160];
};
static_assert(sizeof(Message) < 250);

using Receiver = std::function<void(const Message &)>;

bool begin(const char *peer_mac, uint8_t channel, Receiver receiver);
void adopt_session(const std::string &id);
bool hello();
bool prepare(const std::string &session_id, uint32_t turn, const std::string &text);
bool ready(uint32_t turn);
bool play(uint32_t turn);
bool done(uint32_t turn);
bool stop();

}

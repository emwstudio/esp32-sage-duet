#pragma once
#include "app_state.hpp"
#include <cstddef>
#include <mutex>
#include <string>

class ConversationClient {
public:
    bool health();
    bool create_auto(Turn &turn);
    bool create(const uint8_t *wav, size_t size, Turn &turn);
    bool next(int after_turn, Turn &turn);
    bool download(const char *path, uint8_t *buffer, size_t capacity, size_t &size);
    void adopt_session(const std::string &session_id);
    std::string session_id() const { return session_snapshot(); }
    void cancel();
    const char *last_error() const { return error_.c_str(); }

private:
    bool request_json(const char *method, const char *path,
                      const uint8_t *body, size_t body_size, const char *content_type,
                      std::string &response);
    bool parse_turn(const std::string &json, Turn &turn);
    std::string session_snapshot() const;
    mutable std::mutex session_mutex_;
    std::string session_id_;
    std::string error_;
};

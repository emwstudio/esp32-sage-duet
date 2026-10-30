#pragma once

class Button {
public:
    void init();
    bool clicked();
    bool is_pressed() const;

private:
    int candidate_ = 0;
    int stable_ = 0;
    int count_ = 0;
    bool armed_ = false;
};

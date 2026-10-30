#pragma once

class Button {
public:
    void init();
    bool clicked();
    bool is_pressed() const;

private:
    int candidate_ = -1;
    int stable_ = -1;
    int count_ = 0;
    bool armed_ = false;
    int boot_candidate_ = -1;
    int boot_stable_ = -1;
    int boot_count_ = 0;
    bool boot_armed_ = false;
};

#pragma once

#include <string>

namespace quasar {

    class GPIO {
    public:
        enum class Direction {
            IN = 0,
            OUT
        };
        enum class Edge {
            RISING = 0,
            FALLING,
            BOTH
        };

        GPIO(int number, Direction direction, Edge edge = GPIO::Edge::BOTH);
        // clang-format off
        GPIO(GPIO&& other) noexcept;
        // clang-format on

        GPIO& operator=(GPIO&& other) noexcept;

        void write(const std::string& value);
        int readSync() const;

        int read() const;

        int getHandle() const;

        Edge getEdge() const;

        ~GPIO();

    private:
        int number_;
        Direction direction_;
        int valueFd_ = -1;
        Edge edge_;

        GPIO(const GPIO& other) = delete;
        GPIO& operator=(const GPIO& other) = delete;
        static std::string edgeToString(Edge edge);
    };

} // namespace quasar

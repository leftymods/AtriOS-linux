#pragma once

#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>

namespace quasar {

    class Interleaver {
    public:
        /// Caution: stores pointer to @channels param, make sure it'll live longer than this Interleaver.
        void add(const std::vector<float>& channels, int count);
        void merge(std::vector<float>& result) const;

    private:
        std::deque<std::pair<const std::vector<float>*, int>> channelsSequence_;
    };

} // namespace quasar

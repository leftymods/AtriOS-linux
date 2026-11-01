#pragma once

#include "command_result.h"

#include <speechkit/PhraseSpotter.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

namespace AudioSender {

    class PhraseSpotterWrapper: public SpeechKit::PhraseSpotter::PhraseSpotterListener,
                                public std::enable_shared_from_this<PhraseSpotterWrapper> {
    public:
        using SharedPtr = std::shared_ptr<PhraseSpotterWrapper>;

        explicit PhraseSpotterWrapper(const std::string& spotterModelPath);

        void onPhraseSpotted(SpeechKit::PhraseSpotter::SharedPtr phraseSpotter,
                             const std::string& phrase,
                             int phraseId,
                             const std::string& channelName) override;
        void onPhraseSpotterStarted(SpeechKit::PhraseSpotter::SharedPtr phraseSpotter) override;
        void onPhraseSpotterError(SpeechKit::PhraseSpotter::SharedPtr phraseSpotter, const SpeechKit::Error& error) override;

        CommandResult startManualSend(SpeechKit::AudioSource::SharedPtr source);
        CommandResult startAutoSend(SpeechKit::AudioSource::SharedPtr source, const std::string& extra,
                                    std::chrono::milliseconds beforeTrigger,
                                    std::chrono::milliseconds afterTrigger);
        void stop();
        int getTriggeringCount();

    private:
        CommandResult startSpotterSynchronously();

    private:
        SpeechKit::PhraseSpotter::SharedPtr phraseSpotter;
        std::string spotterModelPath;
        std::atomic<int> triggeringCount;
        std::promise<void> promise;
        std::atomic<bool> started{false};
        bool autoSend = false;
    };

} // namespace AudioSender

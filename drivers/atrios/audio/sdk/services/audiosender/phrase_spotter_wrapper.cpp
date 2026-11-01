#include "phrase_spotter_wrapper.h"

#include <yandex_io/libs/logging/logging.h>

#include <json/json.h>

#include <exception>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace AudioSender;

PhraseSpotterWrapper::PhraseSpotterWrapper(const std::string& spotterModelPath)
    : spotterModelPath(spotterModelPath)
{
}

void PhraseSpotterWrapper::onPhraseSpotted(SpeechKit::PhraseSpotter::SharedPtr phraseSpotter,
                                           const std::string& /* phrase */,
                                           int /* phraseId */,
                                           const std::string& /* channelName */)
{
    if (phraseSpotter == this->phraseSpotter) {
        YIO_LOG_INFO("Spotter triggered");
        ++triggeringCount;
    }
}

void PhraseSpotterWrapper::onPhraseSpotterStarted(SpeechKit::PhraseSpotter::SharedPtr phraseSpotter) {
    if (phraseSpotter == this->phraseSpotter) {
        YIO_LOG_INFO("Spotter started");
        started.store(true);
        promise.set_value();
    }
}

void PhraseSpotterWrapper::onPhraseSpotterError(SpeechKit::PhraseSpotter::SharedPtr phraseSpotter, const SpeechKit::Error& error) {
    if (phraseSpotter == this->phraseSpotter) {
        YIO_LOG_ERROR_EVENT("PhraseSpotterWrapper.Error", error.getString());
        if (!started.load()) {
            promise.set_exception(std::make_exception_ptr(std::runtime_error(error.getString())));
        }
    }
}

CommandResult PhraseSpotterWrapper::startManualSend(SpeechKit::AudioSource::SharedPtr audioSource) {
    if (started.load()) {
        return {false, "Spotter has already started"};
    }

    if (autoSend || !phraseSpotter) {
        phraseSpotter = SpeechKit::PhraseSpotter::create(
            SpeechKit::PhraseSpotterSettings{spotterModelPath}, shared_from_this(), audioSource);
        autoSend = false;
    }

    return startSpotterSynchronously();
}

CommandResult PhraseSpotterWrapper::startAutoSend(SpeechKit::AudioSource::SharedPtr audioSource, const std::string& extra,
                                                  std::chrono::milliseconds beforeTrigger,
                                                  std::chrono::milliseconds afterTrigger) {
    if (started.load()) {
        return {false, "Spotter has already started"};
    }

    // always recreate spotter because of settings changes
    SpeechKit::PhraseSpotterSettings settings{spotterModelPath};
    settings.soundLoggerSettings.soundLengthBeforeTrigger = beforeTrigger;
    settings.soundLoggerSettings.soundLengthAfterTrigger = afterTrigger;
    settings.soundLoggerSettings.payloadExtra = extra;
    settings.soundLoggerSettings.soundFormat = SpeechKit::SoundFormat::PCM;
    phraseSpotter = SpeechKit::PhraseSpotter::create(settings, shared_from_this(), audioSource);
    autoSend = true;

    return startSpotterSynchronously();
}

CommandResult PhraseSpotterWrapper::startSpotterSynchronously() {
    triggeringCount.store(0);
    started.store(false);
    promise = std::promise<void>();
    auto future = promise.get_future();
    phraseSpotter->start();
    try {
        future.wait();
        future.get();
    } catch (std::runtime_error& e) {
        return {false, e.what()};
    }
    return {true, ""};
}

void PhraseSpotterWrapper::stop() {
    if (phraseSpotter) {
        started.store(false);
        phraseSpotter->stop();
    }
}

int PhraseSpotterWrapper::getTriggeringCount() {
    return triggeringCount;
}

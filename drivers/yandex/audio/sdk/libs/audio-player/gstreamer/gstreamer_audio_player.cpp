#include "gstreamer_audio_player.h"

#include "gstreamer_audio_clock.h"
#include "gio_logging_resolver.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <gio/gio.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/audio/gstaudiofilter.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <unistd.h>

using namespace quasar;
using namespace quasar::gstreamer;

YIO_DEFINE_LOG_MODULE("gstreamer");

#define GST_G_IO_MODULE_DECLARE(name) \
    extern void G_PASTE(g_io_, G_PASTE(name, _load))(gpointer module)

#define GST_G_IO_MODULE_LOAD(name)       \
    G_PASTE(g_io_, G_PASTE(name, _load)) \
    (nullptr)

std::once_flag gGstPluginsInit;

extern "C" {

    GST_PLUGIN_STATIC_DECLARE(coreelements);
    GST_PLUGIN_STATIC_DECLARE(audiofx);
    GST_PLUGIN_STATIC_DECLARE(audiomixer);
    GST_PLUGIN_STATIC_DECLARE(audioparsers);
    GST_PLUGIN_STATIC_DECLARE(audiorate);
    GST_PLUGIN_STATIC_DECLARE(audioresample);
    GST_PLUGIN_STATIC_DECLARE(autodetect);
    GST_PLUGIN_STATIC_DECLARE(audioconvert);
    GST_PLUGIN_STATIC_DECLARE(dashdemux);
    GST_PLUGIN_STATIC_DECLARE(gio);
    GST_PLUGIN_STATIC_DECLARE(pbtypes);
    GST_PLUGIN_STATIC_DECLARE(playback);
    GST_PLUGIN_STATIC_DECLARE(soup);
    GST_PLUGIN_STATIC_DECLARE(typefindfunctions);
    GST_PLUGIN_STATIC_DECLARE(volume);
    GST_PLUGIN_STATIC_DECLARE(hls);
    GST_PLUGIN_STATIC_DECLARE(mpegtsdemux);
    GST_PLUGIN_STATIC_DECLARE(app);
    GST_PLUGIN_STATIC_DECLARE(id3demux);
    GST_PLUGIN_STATIC_DECLARE(isomp4);
    GST_PLUGIN_STATIC_DECLARE(spectrum);
    GST_PLUGIN_STATIC_DECLARE(wavparse);
    GST_PLUGIN_STATIC_DECLARE(ogg);
    GST_PLUGIN_STATIC_DECLARE(opus);
    GST_PLUGIN_STATIC_DECLARE(equalizer);

#ifdef QUASAR_GSTREAMER_LIBAV
    GST_PLUGIN_STATIC_DECLARE(libav);
#else
    GST_PLUGIN_STATIC_DECLARE(fdkaac);
    GST_PLUGIN_STATIC_DECLARE(mpg123);
#endif

#ifdef QUASAR_GSTREAMER_OPENSLES
    GST_PLUGIN_STATIC_DECLARE(opensles);
#endif

#ifdef QUASAR_GSTREAMER_ALSA
    GST_PLUGIN_STATIC_DECLARE(alsa_nocapturelog);
#endif

    GST_G_IO_MODULE_DECLARE(openssl);
};

namespace {

    struct InitCtx {
        std::mutex mutex;
        bool isInited = false;
        std::string error;
        std::condition_variable cond;
        const std::string& pipeline;

        InitCtx(const std::string& p)
            : pipeline(p)
                  {};

        void wait() {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock,
                      [this]() {
                          return isInited;
                      });

            if (!error.empty()) {
                throw std::runtime_error(error);
            }
        }

        template <typename Func_>
        void callPipeline(Func_ callback) {
            std::lock_guard<std::mutex> lock(mutex);
            try {
                callback(pipeline);
            } catch (const std::runtime_error& e) {
                error = e.what();
            } catch (...) {
                error = "unknown exception happened during callPipeline";
            }
            isInited = true;
            cond.notify_one();
        }
    };

    const char* gstStateName(GstState state)
    {
        switch (state) {
            case GST_STATE_VOID_PENDING:
                return "GST_STATE_VOID_PENDING";
            case GST_STATE_NULL:
                return "GST_STATE_NULL";
            case GST_STATE_READY:
                return "GST_STATE_READY";
            case GST_STATE_PAUSED:
                return "GST_STATE_PAUSED";
            case GST_STATE_PLAYING:
                return "GST_STATE_PLAYING";
        }
        return "<<Unknown>>";
    }

    std::string preparePluginName(std::string plugin) {
        std::erase_if(plugin, [](char c) {
            return std::isdigit(c);
        });
        return plugin;
    }

    std::string buildLogTag(GstreamerAudioPlayer* _this) {
        std::stringstream ss;
        ss << "[this=" << _this << "] ";
        return ss.str();
    }

    constexpr bool DEFAULT_GLIB_LOGGING = true;
} // namespace

GstreamerAudioPlayer::GstreamerAudioPlayer(std::shared_ptr<Gstreamer> gstreamer, const AudioPlayer::Params& params)
    : AudioPlayer(params)
    , tag_(buildLogTag(this))
    , gstreamer_(std::move(gstreamer))
    , pendingSeekMs_(params.initialOffsetMs())
    , pendingChannel_(params_.channel() == Channel::ALL ? Channel::UNDEFINED : params_.channel())
    , activeChannel_(params_.channel() == Channel::UNDEFINED || params_.channel() == Channel::ALL ? Channel::ALL : Channel::UNDEFINED)
    , syncSlave_(params.mode() == GstreamerAudioPlayer::Params::Mode::SLAVE)
{
    YIO_LOG_DEBUG(tag_ << "GstreamerAudioPlayer call");
}

GstreamerAudioPlayer::~GstreamerAudioPlayer() {
    YIO_LOG_DEBUG(tag_ << "~GstreamerAudioPlayer call " << duration_.load());

    stopped_ = true;
    playing_ = false;

    progressCondVar_.notify_one();
    playStartCondVar_.notify_one();

    if (progressThread_.joinable()) {
        progressThread_.join();
    }

    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    if (mainLoop_) {
        g_main_loop_quit(mainLoop_.get());
    }

    if (mainLoopThread_.joinable()) {
        mainLoopThread_.join();
    }

    if (pipeline_) {
        gst_object_unref(pipeline_);
    }
}

void GstreamerAudioPlayer::init(const std::string& pipeline) {
    YIO_LOG_DEBUG(tag_ << "GstreamerAudioPlayer::init call");
    YIO_LOG_INFO(tag_ << "GstreamerAudioPlayer pipeline: " << pipeline);

    stopped_ = false;
    call_once(gGstPluginsInit, []() {
        // core
        GST_PLUGIN_STATIC_REGISTER(coreelements);

        // base
        GST_PLUGIN_STATIC_REGISTER(app);
        GST_PLUGIN_STATIC_REGISTER(audioconvert);
        GST_PLUGIN_STATIC_REGISTER(audiomixer);
        GST_PLUGIN_STATIC_REGISTER(audiorate);
        GST_PLUGIN_STATIC_REGISTER(audioresample);
        GST_PLUGIN_STATIC_REGISTER(dashdemux);
        GST_PLUGIN_STATIC_REGISTER(gio);
        GST_PLUGIN_STATIC_REGISTER(ogg);
        GST_PLUGIN_STATIC_REGISTER(opus);
        GST_PLUGIN_STATIC_REGISTER(pbtypes);
        GST_PLUGIN_STATIC_REGISTER(playback);
        GST_PLUGIN_STATIC_REGISTER(typefindfunctions);
        GST_PLUGIN_STATIC_REGISTER(volume);

        // good
        GST_PLUGIN_STATIC_REGISTER(audiofx);
        GST_PLUGIN_STATIC_REGISTER(audioparsers);
        GST_PLUGIN_STATIC_REGISTER(autodetect);
        GST_PLUGIN_STATIC_REGISTER(equalizer);
        GST_PLUGIN_STATIC_REGISTER(id3demux);
        GST_PLUGIN_STATIC_REGISTER(isomp4);
        GST_PLUGIN_STATIC_REGISTER(soup);
        GST_PLUGIN_STATIC_REGISTER(spectrum);
        GST_PLUGIN_STATIC_REGISTER(wavparse);

        // bad
        GST_PLUGIN_STATIC_REGISTER(hls);
        GST_PLUGIN_STATIC_REGISTER(mpegtsdemux);

#ifdef QUASAR_GSTREAMER_LIBAV
        GST_PLUGIN_STATIC_REGISTER(libav);
#else
        GST_PLUGIN_STATIC_REGISTER(fdkaac);
        GST_PLUGIN_STATIC_REGISTER(mpg123);
#endif

#ifdef QUASAR_GSTREAMER_OPENSLES
        GST_PLUGIN_STATIC_REGISTER(opensles);
#endif

#ifdef QUASAR_GSTREAMER_ALSA
        GST_PLUGIN_STATIC_REGISTER(alsa_nocapturelog);
#endif

        GST_G_IO_MODULE_LOAD(openssl);

        // Older versions of glib relied on a patch to glib-networking that
        // would enable handling of CA_CERTIFICATES environment variable
        //
        // Since GLib 2.60, there is a way to set SSL certificates path via API
        if (const char* caCertificates = getenv("CA_CERTIFICATES")) {
            YIO_LOG_INFO("Setting glib-networking certificate path to " << caCertificates);
            GTlsBackend* backend = g_tls_backend_get_default();
            if (backend) {
                GTlsDatabase* db = g_tls_file_database_new(caCertificates, nullptr);
                if (db) {
                    g_tls_backend_set_default_database(backend, db);
                } else {
                    YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.Init.CreateGioTlsDatabaseFailed", "Failed to create GIO TLS database while setting certificates path");
                }
            } else {
                YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.Init.GetGioTlsBackendFailed", "Failed to get GIO TLS backend while setting certificates path");
            }
        }
    });

    InitCtx initCtx(pipeline);
    /* WARNING: to avoid intersection of various gstreamer instances we need to allocate different MainContexts.
      Simplest way to specify MainContext for GStreamer's pipeline is set it via 'context_push_thread_default()'.
      So we should init pipeline in MainLoop's thread
    */
    mainLoopThread_ = std::thread([&initCtx, this]() {
        initCtx.callPipeline([this](const std::string& pipeline) {
            initPipeline(pipeline);
        });
        mainLoop();
    });
    initCtx.wait();
    progressThread_ = std::thread(&GstreamerAudioPlayer::progressLoop, this);
}

namespace {
    gboolean gstreamerGetPosition(GstreamerAudioPlayer* player) noexcept {
        return player->updateProgress() ? TRUE : FALSE;
    }

    void gstreamerGetPositionDestroy(gpointer data) noexcept {
        YIO_LOG_DEBUG("getPosition destroyed! " << data);
    }

    void needDataCallback(GstElement* /*appsrc*/, guint /*length*/, quasar::gstreamer::GstreamerAudioPlayer* player) noexcept {
        player->needData();
    }

} // namespace

void GstreamerAudioPlayer::initPipeline(const std::string& pipeline) {
    mainContext_.reset(g_main_context_new());
    if (!mainContext_) {
        throw std::runtime_error("g_main_cntext_new failed");
    }

    mainLoop_.reset(g_main_loop_new(mainContext_.get(), false));

    if (!mainLoop_) {
        throw std::runtime_error("g_main_loop_new failed");
    }

    {
        auto timerSource = g_timeout_source_new(1000);

        if (!timerSource) {
            throw std::runtime_error("Cannot allocate source for timer");
        }

        g_source_set_callback(timerSource, (GSourceFunc)gstreamerGetPosition, this, gstreamerGetPositionDestroy);
        g_source_attach(timerSource, mainContext_.get());
        g_source_set_priority(timerSource, G_PRIORITY_DEFAULT);
        g_source_unref(timerSource);
    }

    g_main_context_push_thread_default(mainContext_.get());

    GError* error = nullptr;
    pipeline_ = gst_parse_launch(pipeline.c_str(), &error);
    if (error) {
        std::string message = std::string("Unable to build pipeline: ") + error->message;
        g_error_free(error);

        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.InitPipeline.ParseLaunchFailed", tag_ << "Pipeline build error: " << message);

        throw std::runtime_error(message);
    }

    appSrcElement_.reset();
    isEndOfStream_ = false;

    if (auto streamSrc = params().streamSrc()) {
        const auto appSrcName = streamSrc->getName();
        GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline_), appSrcName.c_str());

        if (!src) {
            throw std::runtime_error("appsrc element \"" + appSrcName + "\" not found");
        }

        dataStream_ = std::move(streamSrc);
        appSrcElement_.reset(src);

        g_signal_connect(src, "need-data", G_CALLBACK(needDataCallback), this);
    }

    if (auto clock = params().audioClock()) {
        if (auto clockImpl = dynamic_cast<const GstreamerAudioClock*>(clock.get())) {
            if (auto gstClock = static_cast<GstClock*>(clockImpl->gstClock())) {
                if (!gst_clock_is_synced(gstClock)) {
                    throw std::runtime_error("Fail to set pipeline clock. The clock is unsynchronized, try late...");
                }
                gst_pipeline_use_clock(GST_PIPELINE(pipeline_), gstClock);
                gst_element_set_start_time(pipeline_, GST_CLOCK_TIME_NONE);
                gst_pipeline_set_latency(GST_PIPELINE(pipeline_), GST_SECOND / 2);
                syncStandalone_ = false;
            }
        }
    }

    if (auto capsfilter = gst_bin_get_by_name(GST_BIN(pipeline_), "capsfilter0")) {
        /*
         * Save original "channel" and "channel-mask" for Channel::ALL request
         */
        GstCaps* caps = nullptr;
        g_object_get(G_OBJECT(capsfilter), "caps", &caps, nullptr);
        if (caps) {
            if (gst_caps_get_size(caps) == 1) {
                if (auto structure = gst_caps_get_structure(caps, 0)) {
                    const char* prefix = "audio/";
                    auto name = gst_structure_get_name(structure);
                    if (name && strncmp(prefix, name, strlen(prefix)) == 0) {
                        int n = gst_structure_n_fields(structure);
                        for (int i = 0; i < n; ++i) {
                            name = gst_structure_nth_field_name(structure, i);
                            if (auto value = gst_structure_get_value(structure, name)) {
                                auto gtype = G_VALUE_TYPE(value);
                                if (gtype == G_TYPE_INT && strcmp(name, "channels") == 0) {
                                    originalChannels_ = g_value_get_int(value);
                                } else if (gtype == GST_TYPE_BITMASK && strcmp(name, "channel-mask") == 0) {
                                    originalChannelMask_ = gst_value_get_bitmask(value);
                                }
                            }
                        }
                    }
                }
            }
            gst_caps_unref(caps);
        }
        g_object_unref(capsfilter);
    }

    // Register bus observer for pipeline
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
    // id of source returned from next func is attached to mainContext_. g_source_remove is unneeded
    gst_bus_add_watch(bus, &GstreamerAudioPlayer::onBusMessage, this);
    gst_object_unref(bus);

    volumeController_ = std::make_unique<VolumeController>(pipeline_);
    equalizer_ = std::make_unique<Equalizer>(pipeline_);
}

void GstreamerAudioPlayer::needData() {
    GstFlowReturn ret = GST_FLOW_OK;
    auto data = stopped_ ? StreamSrc::Data() : dataStream_->pullData();
    if (data.empty()) {
        if (!isEndOfStream_) {
            g_signal_emit_by_name(appSrcElement_.get(), "end-of-stream", &ret);
            isEndOfStream_ = true;
        }
        return;
    }
    GstBuffer* buffer = gst_buffer_new_and_alloc(data.size());
    gst_buffer_fill(buffer, 0, data.data(), data.size());

    const auto frames = data.size() / (dataStream_->getChannelsCount() * dataStream_->getSampleSize());
    constexpr std::chrono::nanoseconds nsInSec = std::chrono::seconds(1);
    const auto chunkDurationNs = frames * (1 / (double)dataStream_->getSampleRate()) * nsInSec.count();

    // setup chunk timestamp (progress) and duration
    // so gstreamer will call callbacks using this data
    // i.e.: EOS will be called when last sample will be played
    GST_BUFFER_PTS(buffer) = streamProgressTs_;
    GST_BUFFER_DURATION(buffer) = chunkDurationNs;
    streamProgressTs_ += GST_BUFFER_DURATION(buffer);

    g_signal_emit_by_name(appSrcElement_.get(), "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
    if (ret != GST_FLOW_OK && !stopped_) {
        throw std::runtime_error("Can't push buffer");
    }
}

std::optional<GstreamerAudioPlayer::SyncParams> GstreamerAudioPlayer::syncParams() const {
    if (syncStandalone_) {
        return std::nullopt;
    }

    return SyncParams{playing_, std::chrono::nanoseconds{syncBaseTime_}, std::chrono::nanoseconds{syncPosition_}};
}

Json::Value GstreamerAudioPlayer::debug() const {
    Json::Value result;

    const auto& p = params();
    auto& jParams = result["params"];
    jParams["alsaDevice"] = p.alsaDevice();
    jParams["uri"] = p.uri();
    jParams["filePath"] = p.filePath();
    jParams["gstPipeline"] = p.gstPipeline();
    jParams["gstPipelineProcessed"] = p.gstPipelineProcessed();
    jParams["onShotMode"] = p.onShotMode();
    jParams["initialOffsetMs"] = p.initialOffsetMs();
    if (auto ac = p.audioClock()) {
        auto& jClock = jParams["clock"];
        jClock["clockId"] = std::string(ac->clockId());
        jClock["syncLevel"] = AudioClock::syncLevelToText(ac->syncLevel());
    }
    jParams["mode"] = (int)p.mode();
    jParams["channel"] = (int)p.channel();
    if (auto cs = p.streamSrc()) {
        auto& jStream = jParams["streamSrc"];
        jStream["name"] = cs->getName();
        jStream["mediaTypeString"] = cs->getMediaTypeString();
        jStream["useVolumeElementStub"] = cs->useVolumeElementStub();
        jStream["getName"] = cs->getName();
    }
    jParams["isStreamMode"] = p.isStreamMode();
    jParams["bufferStalledThrottle"] = p.bufferStalledThrottle();

    const auto& normalization = p.normalization();
    if (normalization.has_value()) {
        auto& jNormalization = jParams["normalization"];
        jNormalization["truePeak"] = normalization->truePeak;
        jNormalization["integratedLoudness"] = normalization->integratedLoudness;
        jNormalization["targetLufs"] = normalization->targetLufs;
    }

    if (auto volumeBin = gst_bin_get_by_name(GST_BIN(pipeline_), "volume0")) {
        auto& jVolume0 = result["volume0"];
        gdouble v;
        g_object_get(G_OBJECT(volumeBin), "volume", &v, nullptr);
        jVolume0["volume"] = v;
        gboolean m;
        g_object_get(G_OBJECT(volumeBin), "mute", &m, nullptr);
        jVolume0["mute"] = m;
        g_object_unref(volumeBin);
    }

    if (auto normalizationVolumeBin = gst_bin_get_by_name(GST_BIN(pipeline_), "normalization")) {
        auto& jNormalization = result["normalization"];
        gdouble v;
        g_object_get(G_OBJECT(normalizationVolumeBin), "volume", &v, nullptr);
        jNormalization["volume"] = v;
        gboolean m;
        g_object_get(G_OBJECT(normalizationVolumeBin), "mute", &m, nullptr);
        jNormalization["mute"] = m;
        g_object_unref(normalizationVolumeBin);
    }

    if (auto capsfilter = gst_bin_get_by_name(GST_BIN(pipeline_), "capsfilter0")) {
        GstCaps* caps = nullptr;
        g_object_get(G_OBJECT(capsfilter), "caps", &caps, nullptr);
        if (caps) {
            auto txt = gst_caps_to_string(caps);
            result["caps_text"] = txt;
            g_free(txt);

            result["caps"] = Json::arrayValue;
            for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
                auto& jcaps = result["caps"][i];

                if (auto structure = gst_caps_get_structure(caps, i)) {
                    auto name = gst_structure_get_name(structure);
                    if (!name || !*name) {
                        name = "noname";
                    }
                    auto& jstruct = jcaps["structure"][name];
                    for (int j = 0; j < gst_structure_n_fields(structure); ++j) {
                        name = gst_structure_nth_field_name(structure, j);
                        if (auto value = gst_structure_get_value(structure, name)) {
                            auto str = gst_value_serialize(value);
                            jstruct[name] = str;
                            g_free(str);
                        }
                    }
                }
                auto features = gst_caps_get_features(caps, i);
                auto fstr = gst_caps_features_to_string(features);
                jcaps["features"] = fstr;
                g_free(fstr);
            }
            gst_caps_unref(caps);
        }
        g_object_unref(capsfilter);
    }

    result["pendingChannel"] = (int)pendingChannel_.load();
    result["activeChannel"] = (int)activeChannel_.load();
    if (originalChannels_) {
        result["originalChannels"] = *originalChannels_;
    } else {
        result["originalChannels"] = Json::nullValue;
    }
    if (originalChannelMask_) {
        result["originalChannelMask"] = *originalChannelMask_;
    } else {
        result["originalChannelMask"] = Json::nullValue;
    }
    result["syncSlave"] = syncSlave_;
    result["syncStandalone"] = syncStandalone_;
    if (!syncStandalone_) {
        result["syncBaseTime"] = syncBaseTime_;
        result["syncPosition"] = syncPosition_;
    }
    result["pendingSeekMs"] = pendingSeekMs_.load();
    return result;
}

void GstreamerAudioPlayer::mainLoop() {
    g_main_loop_run(mainLoop_.get());
    if (appSrcElement_) {
        sendEnd();
    }
    g_main_context_pop_thread_default(mainContext_.get());
}

void GstreamerAudioPlayer::progressLoop() {
    while (!stopped_) {
        std::unique_lock lock(progressMutex_);
        YIO_LOG_DEBUG(tag_ << "waiting for playing notify");
        playStartCondVar_.wait(lock, [this]() { return stopped_ || playing_.load(); });
        lock.unlock();

        if (stopped_) {
            break;
        }

        while (playing_) {
            if (positionUpdated_.exchange(false)) {
                YIO_LOG_DEBUG(tag_ << "updating progress");
                sendProgress();
            }

            gint lastBufferingPercent = bufferingPercent_;
            if (!waitProgressTimeout()) {
                YIO_LOG_DEBUG(tag_ << "Progress thread wakeup before progress timeout expired");
            }
            checkBufferingState(lastBufferingPercent);

            if (stopped_) {
                break;
            }
        }
    }
}

/*
 * Method called only from progressLoop() once per second during playing track
 * If player felt into buffering state during playback, need to check if downloading stalled
 * and report to upstream listener then
 */
void GstreamerAudioPlayer::checkBufferingState(int lastBufferingPercent) {
    if (buffering_ && bufferingPercent_ == lastBufferingPercent) {
        YIO_LOG_WARN(tag_ << "BufferStalled");
        sendBufferStalled();
    }
}

bool GstreamerAudioPlayer::waitProgressTimeout() {
    std::unique_lock lock(progressMutex_);
    return progressCondVar_.wait_for(lock,
                                     std::chrono::duration<double, std::milli>(1000)) != std::cv_status::no_timeout;
}

gboolean GstreamerAudioPlayer::onBusMessage(GstBus* /* bus */, GstMessage* message, gpointer player) {
    return static_cast<GstreamerAudioPlayer*>(player)->handleBusMessage(message);
}

gboolean GstreamerAudioPlayer::handleBusMessage(GstMessage* message) {
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ASYNC_DONE: {
            asyncDone_.store(true);
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED: {
            durationWasChanged_ = true;
            break;
        }
        case GST_MESSAGE_EOS: {
            if (appSrcElement_) {
                g_main_loop_quit(mainLoop_.get());
            } else if (GST_MESSAGE_SRC(message) == GST_OBJECT_CAST(pipeline_)) {
                setStatePaused();
                sendEnd();
                playing_ = false;
            }
            break;
        }
        case GST_MESSAGE_CLOCK_LOST: {
            YIO_LOG_INFO(tag_ << "GST_MESSAGE_CLOCK_LOST");
            /* Get a new clock */
            setStatePaused();
            setStatePlaying();
            break;
        }
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            gst_message_parse_buffering(message, &percent);

            if (percent < 100) {
                if (!buffering_) {
                    YIO_LOG_INFO(tag_ << "Buffering started");
                    buffering_ = true;
                    sendBufferingStart();
                }
            } else {
                YIO_LOG_INFO(tag_ << "Buffering: " << percent << "%");
                if (buffering_) {
                    YIO_LOG_INFO(tag_ << "Buffering stopped");
                    buffering_ = false;
                    sendBufferingEnd();
                }
            }
            bufferingPercent_ = percent;
            break;
        }
        case GST_MESSAGE_ERROR: {
            GError* error;
            gchar* debugInfo;

            gst_message_parse_error(message, &error, &debugInfo);
            std::stringstream errorMessage;
            errorMessage << std::string("GST_MESSAGE_ERROR from ") << preparePluginName(GST_MESSAGE_SRC_NAME(message)) << ": " << error->message;
            const auto errorStr = errorMessage.str();

            YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.ErrorMessage", tag_ << errorStr);
            YIO_LOG_WARN(tag_ << "gst error debugInfo: " << debugInfo);
            g_error_free(error);
            g_free(debugInfo);
            sendError(errorStr);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(message) == GST_OBJECT_CAST(pipeline_)) {
                GstState old_state;
                GstState new_state;
                gst_message_parse_state_changed(message, &old_state, &new_state, nullptr);

                YIO_LOG_INFO(tag_ << "State changed from: " << gstStateName(old_state) << " to: " << gstStateName(new_state));
                if (pendingSeekMs_ >= 0) {
                    seek(pendingSeekMs_.exchange(-1));
                }

                auto ch = pendingChannel_.exchange(Channel::UNDEFINED);
                if (ch != Channel::UNDEFINED) {
                    setChannel(ch);
                }

                if (new_state == GST_STATE_PLAYING) {
                    if (wasStarted_) {
                        sendResumed();
                    } else {
                        wasStarted_ = true;
                        sendStart();
                    }
                    playing_ = true;
                    playStartCondVar_.notify_one();
                } else if (old_state == GST_STATE_PLAYING && new_state == GST_STATE_PAUSED) {
                    if (wasStopped_) {
                        sendStopped();
                    } else if (playing_) { // eos came before state changing, so we can be already stopped
                        sendPaused();
                    }
                    playing_ = false;
                }
                stateChangedCondVar_.notify_all();
            } else if (g_str_has_prefix(GST_MESSAGE_SRC_NAME(message), "tsdemux")) {
                // search for sink element
                // TODO: rework if more than 1 sink is used
                GstElement* sinkElement = nullptr;
                auto* it = gst_bin_iterate_sinks(GST_BIN(pipeline_));
                GValue item = G_VALUE_INIT;
                auto done = FALSE;
                while (!done) {
                    switch (gst_iterator_next(it, &item)) {
                        case GST_ITERATOR_OK: {
                            sinkElement = static_cast<GstElement*>(g_value_get_object(&item));
                            GstState newState;
                            gst_message_parse_state_changed(message, nullptr, &newState, nullptr);
                            YIO_LOG_INFO(tag_ << "tsdemux state change: " << gstStateName(newState));
                            if (GST_STATE_NULL == newState) {
                                g_object_set(sinkElement, "sync", TRUE, NULL);
                                YIO_LOG_INFO(GST_OBJECT_NAME(sinkElement) << " sync to true");
                            } else {
                                g_object_set(sinkElement, "sync", FALSE, NULL);
                                YIO_LOG_INFO(GST_OBJECT_NAME(sinkElement) << " sync to false");
                            }
                            g_value_reset(&item);
                            break;
                        }
                        case GST_ITERATOR_RESYNC: {
                            gst_iterator_resync(it);
                            break;
                        }
                        case GST_ITERATOR_ERROR: {
                            done = TRUE;
                            break;
                        }
                        case GST_ITERATOR_DONE: {
                            done = TRUE;
                            break;
                        }
                    }
                }

                // freeing resources
                g_value_unset(&item);
                gst_iterator_free(it);
            }

            break;
        }
        case GST_MESSAGE_ELEMENT: {
            const GstStructure* s = gst_message_get_structure(message);
            if (s == nullptr) {
                break;
            }

            const gchar* name = gst_structure_get_name(s);
            if (strcmp(name, "spectrum") != 0) {
                break;
            }

            const GstObject* src = GST_MESSAGE_SRC(message);

            GstClockTime runningTime;
            if (!gst_structure_get_clock_time(s, "running-time", &runningTime)) {
                YIO_LOG_WARN(tag_ << "Failed to get running time, skipping spectrum message");
                break;
            }

            const GstClockTime clockTime = gst_clock_get_time(GST_ELEMENT_CLOCK(src));
            if (!GST_CLOCK_TIME_IS_VALID(clockTime)) {
                YIO_LOG_WARN(tag_ << "Failed to get clock time, skipping spectrum message");
                break;
            }

            const GstClockTime baseTime = gst_element_get_base_time(GST_ELEMENT_CAST(src));
            if (!GST_CLOCK_TIME_IS_VALID(baseTime)) {
                YIO_LOG_WARN(tag_ << "Failed to get element base time, skipping spectrum message");
                break;
            }

            GstClockTime pipelineLatency = gst_pipeline_get_latency(GST_PIPELINE(pipeline_));
            if (!GST_CLOCK_TIME_IS_VALID(pipelineLatency)) {
                // latency may not be set
                pipelineLatency = 0;
            }

            const GstClockTimeDiff clockRunningTime = GST_CLOCK_DIFF(baseTime, clockTime);

            const auto delay = std::chrono::nanoseconds(GST_CLOCK_DIFF(clockRunningTime, runningTime + pipelineLatency));

            // XXX use AudioClock
            auto systemClock = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());

            GstClockTime duration;
            if (!gst_structure_get_clock_time(s, "duration", &duration)) {
                duration = GST_CLOCK_TIME_NONE;
            }

            const GValue* magnitude = gst_structure_get_value(s, "magnitude");
            const gint bands = gst_value_list_get_size(magnitude);

            gint threshold;
            g_object_get(G_OBJECT(src), "threshold", &threshold, nullptr);

            Listener::SpectrumFrame frame;
            frame.runningTime = systemClock + delay;
            frame.duration = std::chrono::nanoseconds(duration);
            frame.rate = GST_AUDIO_FILTER_RATE(src);
            frame.magnitudes.reserve(bands);
            for (gint i = 0; i < bands; i++) {
                const GValue* mag = gst_value_list_get_value(magnitude, i);
                frame.magnitudes.push_back(g_value_get_float(mag));
            }
            frame.threshold = threshold;

            sendSpectrum(frame);

            break;
        }
        default: {
            break;
        }
    }

    return true;
}

bool GstreamerAudioPlayer::updateProgress() {
    if (!pipeline_) {
        return false;
    }
    if (asyncDone_ && playing_) {
        if (durationIsAvailable_.value_or(true) && durationWasChanged_) {
            gint64 len;
            // test that duration is available for this stream
            if (gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &len)) {
                const auto prev = duration_.exchange(len / 1000000000);
                if (prev != duration_.load()) {
                    positionUpdated_.store(true);
                    progressCondVar_.notify_one();
                }
                durationIsAvailable_ = true;
            } else {
                if (durationIsAvailable_.has_value()) {
                    // if stream had duration at some point -- duration request should not fail
                    YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.QueryDurationFailed", tag_ << "gst_element_query_duration fail");
                } else {
                    YIO_LOG_INFO(tag_ << "Duration is not available for this type of stream");
                    durationIsAvailable_ = false;
                }
            }
            durationWasChanged_ = false;
        }
        gint64 pos;
        if (gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos)) {
            auto prev = position_.exchange(pos / 1000000000);
            if (prev != position_.load()) {
                positionUpdated_.store(true);
                progressCondVar_.notify_one();
            }
        } else {
            YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.QueryPositionFailed", tag_ << "gst_element_query_position fail");
        }
    }
    return true;
}

bool GstreamerAudioPlayer::startBuffering() {
    YIO_LOG_DEBUG(tag_ << "GstreamerAudioPlayer::startBuffering call");
    return pause();
}

bool GstreamerAudioPlayer::setVolume(double volume) {
    return volumeController_->setVolume(volume);
}

GstreamerAudioPlayer::Channel GstreamerAudioPlayer::channel() {
    auto ch = pendingChannel_.load();
    return ch != Channel::UNDEFINED ? ch : activeChannel_.load();
}

bool GstreamerAudioPlayer::setChannel(Channel ch) {
    if (dataStream_) {
        // disable stereo-pair channel setup for AppSrc players
        return false;
    }

    if (ch == Channel::UNDEFINED) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetChannel.InvalidChannel", tag_ << "wrong chanel");
        return false;
    }

    if (ch == activeChannel_) {
        return false;
    }

    GstState currentState = GST_STATE_NULL;
    GstState pendingState = GST_STATE_NULL;
    gst_element_get_state(pipeline_, &currentState, &pendingState, 0);
    if (currentState != pendingState && pendingState) {
        pendingChannel_ = ch;
        return true;
    }
    switch (currentState) {
        case GST_STATE_PLAYING:
            setStatePaused(); // Stop before change sound channel
            break;
        case GST_STATE_PAUSED:
            // do nothing
            break;
        default:
            pendingChannel_ = ch;
            return true;
    }

    std::string errorMessage;
    if (auto capsfilter = gst_bin_get_by_name(GST_BIN(pipeline_), "capsfilter0"))
    {
        GstCaps* caps = nullptr;
        g_object_get(G_OBJECT(capsfilter), "caps", &caps, nullptr);
        if (caps) {
            if (gst_caps_get_size(caps) == 1) {
                if (auto structure = gst_caps_get_structure(caps, 0)) {
                    const char* prefix = "audio/";
                    auto name = gst_structure_get_name(structure);
                    if (name && strncmp(prefix, name, strlen(prefix)) == 0) {
                        auto newCaps = gst_caps_copy(caps);
                        if (ch == Channel::ALL) {
                            if (originalChannels_) {
                                auto channels = *originalChannels_;
                                gst_structure_set(structure, "channels", G_TYPE_INT, channels, nullptr);
                            }
                            if (originalChannelMask_) {
                                auto bitmask = *originalChannelMask_;
                                gst_structure_set(structure, "channel-mask", GST_TYPE_BITMASK, bitmask, nullptr);
                            }
                        } else if (ch == Channel::LEFT) {
                            gst_caps_set_simple(newCaps, "channels", G_TYPE_INT, 1, nullptr);
                            gst_caps_set_simple(newCaps, "channel-mask", GST_TYPE_BITMASK, 0x1ULL, nullptr);
                        } else if (ch == Channel::RIGHT) {
                            gst_caps_set_simple(newCaps, "channels", G_TYPE_INT, 1, nullptr);
                            gst_caps_set_simple(newCaps, "channel-mask", GST_TYPE_BITMASK, 0x2ULL, nullptr);
                        } else {
                            errorMessage = "Fail to change sound channel, unknown channel enum: " + std::to_string((int)ch);
                        }
                        if (errorMessage.empty()) {
                            g_object_set(capsfilter, "caps", newCaps, nullptr);
                            activeChannel_ = ch;
                        }
                        gst_caps_unref(newCaps);
                    } else {
                        errorMessage = "Fail to change sound channel, can modify channels only for \"audio/*\" caps";
                    }
                } else {
                    errorMessage = "Fail to change sound channel, can't get caps structure";
                }
            }
            gst_caps_unref(caps);
        }
        g_object_unref(capsfilter);
    } else if (ch != Channel::ALL) {
        errorMessage = "Fail to change sound channel, elemnt \"capsfilter0\" not found in pipeline configuration";
    }

    if (currentState == GST_STATE_PLAYING) {
        setStatePlaying(); // Restore correct state
    }

    if (!errorMessage.empty()) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetChannel.Error", tag_ << errorMessage);
    }
    return errorMessage.empty();
}

bool GstreamerAudioPlayer::setEqualizerConfig(const YandexIO::EqualizerConfig& config) {
    YIO_LOG_DEBUG(tag_ << "GstreamerAudioPlayer::setEqualizerConfig");

    if (config.bands.empty()) {
        equalizer_->setNumBands(1);
        equalizer_->setBandParams(0, 0.0, 0.0, 0.0, std::nullopt);
        equalizer_->setPreampGain(0.0);
        return true;
    }

    if (!equalizer_->setNumBands(config.bands.size())) {
        return false;
    }

    double maxGain = 0.0f;

    for (size_t i = 0; i < config.bands.size(); i++) {
        const auto& band = config.bands[i];

        if (!equalizer_->setBandParams(i, band.freq, band.width, band.gain, band.type)) {
            return false;
        }

        maxGain = std::max(maxGain, band.gain);
    }

    if (config.preventClipping) {
        equalizer_->setPreampGain(-maxGain);
    } else {
        equalizer_->setPreampGain(0.0);
    }

    return true;
}

bool GstreamerAudioPlayer::pause() {
    YIO_LOG_DEBUG(tag_ << "GstreamerAudioPlayer::pause call");

    GstState currentState;
    if (gst_element_get_state(pipeline_, &currentState, nullptr, 0) == GST_STATE_CHANGE_FAILURE) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.Pause.GetStateFailed", tag_ << "gst_element_get_state fail");
        return false;
    }

    if (currentState == GST_STATE_PAUSED) {
        YIO_LOG_INFO(tag_ << "already in paused state");
        return false;
    }

    if (setStatePaused() == GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    return true;
}

bool GstreamerAudioPlayer::replayAsync() {
    if (syncSlave_) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.ReplayAsync.NotSupportedInSlaveMode", tag_ << "\"replayAsync\" method is not supported in slave mode");
        return false;
    }
    seek(0);
    return playAsync();
}

bool GstreamerAudioPlayer::playAsync() {
    YIO_LOG_DEBUG(tag_ << "GstreamerAudioPlayer::playAsync call");

    if (syncSlave_) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.PlayAsync.NotSupportedInSlaveMode", tag_ << "\"playAsync\" method is not supported in slave mode");
        return false;
    }

    if (wasStopped_) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.PlayAsync.PlayAfterStop", tag_ << "\"playAsync\" method is not supported after stop");
        return false;
    }

    std::unique_lock lock(progressMutex_);

    GstState currentState;
    if (gst_element_get_state(pipeline_, &currentState, nullptr, 0) == GST_STATE_CHANGE_FAILURE) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.PlayAsync.GetStateFailed", tag_ << "gst_element_get_state fail");
        return false;
    }

    if (currentState == GST_STATE_PLAYING) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.PlayAsync.AlreadyPlaying", tag_ << "already in playing state");
        return false;
    }

    if (setStatePlaying() == GST_STATE_CHANGE_FAILURE) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.PlayAsync.SetPlayingStateFailed", tag_ << "gst_element_set_state playing fail");
        return false;
    }

    return true;
}

bool GstreamerAudioPlayer::playMultiroom(std::chrono::nanoseconds basetime, std::chrono::nanoseconds position)
{
    if (!syncSlave_) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.PlayMultiroom.NotSupportedInMasterMode", tag_ << "The \"playMultiroom\" method is not available for this mode.");
        return false;
    }

    std::unique_lock lock(progressMutex_);
    setStatePaused();
    syncBaseTime_ = basetime.count();
    syncPosition_ = position.count();
    setStatePlaying();
    return true;
}

bool GstreamerAudioPlayer::seek(int ms) {
    if (syncSlave_) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.Seek.NotSupportedInSlaveMode", tag_ << "\"seek\" method is not supported in slave mode");
        return false;
    }

    if (ms < 0) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.Seek.InvalidValue", tag_ << "wrong seek");
        return false;
    }

    GstState currentState = GST_STATE_NULL;
    GstState pendingState = GST_STATE_NULL;
    gst_element_get_state(pipeline_, &currentState, &pendingState, 0);
    if (currentState != pendingState && pendingState) {
        pendingSeekMs_ = ms;
        return true;
    }
    switch (currentState) {
        case GST_STATE_PLAYING:
            YIO_LOG_DEBUG(tag_ << "Stop gstreamer before seek");
            setStatePaused(); // Stop before seeking to synchronize time and position for multiroom case
            break;
        case GST_STATE_PAUSED:
            // do nothing
            break;
        default:
            pendingSeekMs_ = ms;
            return true;
    }

    gint64 nanoseconds = static_cast<gint64>(ms) * 1000 * 1000;
    bool wasSeeked = false;
    if (syncStandalone_) {
        YIO_LOG_DEBUG(tag_ << "Seek to position " << (int)ms << " in standalone mode");
        wasSeeked = gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), nanoseconds);
    } else {
        YIO_LOG_DEBUG(tag_ << "Seek to position " << (int)ms << " in multiroom mode");
        syncPosition_ = nanoseconds;
        wasSeeked = true;
    }

    if (currentState == GST_STATE_PLAYING) {
        setStatePlaying(); // Restore correct state
    }

    if (wasSeeked) {
        YIO_LOG_DEBUG(tag_ << "Seek successful");
        sendSeeked();
    } else {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.Seek.SeekFailed", tag_ << "gst_element_seek_simple fail");
    }

    return wasSeeked;
}

bool GstreamerAudioPlayer::stop() {
    wasStopped_ = true;
    GstState currentState = GST_STATE_NULL;
    gst_element_get_state(pipeline_, &currentState, nullptr, 0);
    if (currentState == GST_STATE_PAUSED) {
        // was paused during stop, so just notify listeners and return
        sendStopped();
        return true;
    }
    auto changeState = gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    if (changeState == GST_STATE_CHANGE_FAILURE) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.Stop.SetStateFailed", tag_ << "gst_element_set_state stop fail");
        return false;
    }
    return true;
}

GstStateChangeReturn GstreamerAudioPlayer::setStatePaused()
{
    GstState currentState = GST_STATE_NULL;
    gst_element_get_state(pipeline_, &currentState, nullptr, 0);

    if (currentState == GST_STATE_PAUSED) {
        return GST_STATE_CHANGE_SUCCESS;
    }

    auto changeState = gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    if (changeState == GST_STATE_CHANGE_FAILURE) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetStatePaused.SetStateFailed", tag_ << "gst_element_set_state paused fail");
    }

    syncBaseTime_ = 0;
    gst_element_query_position(pipeline_, GST_FORMAT_TIME, &syncPosition_);

    return changeState;
}

GstStateChangeReturn GstreamerAudioPlayer::setStatePlaying()
{
    if (!syncStandalone_) {
        if (!syncSlave_) {
            if (GstClock* gstClock = gst_pipeline_get_clock(GST_PIPELINE(pipeline_))) {
                syncBaseTime_ = gst_clock_get_time(gstClock);
                gst_object_unref(gstClock);
            } else {
                YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetStatePlaying.GetClockFailed", tag_ << "fail to get pipeline clock");
                return GST_STATE_CHANGE_FAILURE;
            }
            if (pendingSeekMs_ >= 0) {
                syncPosition_ = pendingSeekMs_.exchange(-1) * 1000LL * 1000LL;
            }
        }

        {
            std::unique_lock lock(stateCvMutex_);
            if (syncPosition_ > 0) {
                GstState currentState = GST_STATE_NULL;
                if (gst_element_get_state(pipeline_, &currentState, nullptr, 0) == GST_STATE_CHANGE_FAILURE) {
                    YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetStatePlaying.GetStateFailed", tag_ << "Fail to get current gst state");
                    return GST_STATE_CHANGE_FAILURE;
                }
                if (currentState != GST_STATE_PAUSED) {
                    auto changeState = gst_element_set_state(pipeline_, GST_STATE_PAUSED);
                    if (changeState == GST_STATE_CHANGE_FAILURE) {
                        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetStatePlaying.SetPausedStateFailed", tag_ << "gst_element_set_state paused fail");
                        return GST_STATE_CHANGE_FAILURE;
                    }
                }
            }
            auto noPendingState = [pipeline = pipeline_] {
                GstState pendingState = GST_STATE_NULL;
                gst_element_get_state(pipeline, nullptr, &pendingState, 0);
                return pendingState == GST_STATE_VOID_PENDING;
            };
            constexpr auto iterationTimeout = std::chrono::seconds{1};
            constexpr auto iterationCount = size_t{5};
            bool successful = false;
            for (size_t iteration = 0; iteration < iterationCount; ++iteration) {
                if (!stateChangedCondVar_.wait_for(lock, iterationTimeout, noPendingState)) {
                    if (YIO_LOG_DEBUG_ENABLED()) {
                        GstState currentState = GST_STATE_NULL;
                        GstState pendingState = GST_STATE_NULL;
                        gst_element_get_state(pipeline_, &currentState, &pendingState, 0);
                        YIO_LOG_DEBUG(tag_ << "Very long wait time to pause: "
                                           << "iteration=" << iteration + 1
                                           << ", currentState=" << gstStateName(currentState)
                                           << ", pendingState=" << gstStateName(pendingState));
                    }
                } else {
                    successful = true;
                    break;
                }
            }
            if (!successful) {
                YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetStatePlaying.StateChangedTimeout", tag_ << "Timeout exceeded for gst_element_set_state change pendingState");
            }
        }

        const auto gstSeekFlags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
        gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, gstSeekFlags, syncPosition_);
        gst_element_set_base_time(pipeline_, syncBaseTime_);
    }

    auto changeState = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (changeState == GST_STATE_CHANGE_FAILURE) {
        YIO_LOG_ERROR_EVENT("GstreamerAudioPlayer.SetStatePlaying.SetPlayingStateFailed", tag_ << "gst_element_set_state playing fail");
    }
    return changeState;
}

const std::vector<AudioPlayer::Format>& GstreamerAudioPlayer::supportedFormats() const {
    static std::vector<AudioPlayer::Format> formats{AudioPlayer::Format::FMT_MP3, AudioPlayer::Format::FMT_WAV};
    return formats;
}

GstreamerAudioPlayer::VolumeController::VolumeController(GstElement* pipeline) {
    /* GstreamerAudioPlayer assumes two pipeline formats in order to control
     * volume:
     * - there can be explicit 'volume' bin with the name set to 'volume0',
     *   that exports 'volume' property
     *
     * - there can be some another volume controller inside the 'pipeline' that
     *   holds 'volume' property.
     *
     * So first try to acquire 'volume0', otherwise fallback to control
     * volume through pipeline.
     */

    auto volumeBin = gst_bin_get_by_name(GST_BIN(pipeline), "volume0");

    if (volumeBin) {
        elementOwned_ = true;
        gstVolumeElement_ = volumeBin;
    } else {
        elementOwned_ = false;
        gstVolumeElement_ = pipeline;
    }
}

GstreamerAudioPlayer::VolumeController::~VolumeController() {
    if (elementOwned_) {
        g_object_unref(gstVolumeElement_);
    }
}

bool GstreamerAudioPlayer::VolumeController::setVolume(double volume) const {
    g_object_set(gstVolumeElement_, "volume", volume, nullptr);
    return true;
}

GstreamerAudioPlayer::Equalizer::Equalizer(GstElement* pipeline) {
    equalizer_ = gst_bin_get_by_name(GST_BIN(pipeline), "equalizer");
    volume_ = gst_bin_get_by_name(GST_BIN(pipeline), "equalizer-preamp");
}

GstreamerAudioPlayer::Equalizer::~Equalizer() {
    if (equalizer_) {
        g_object_unref(equalizer_);
    }

    if (volume_) {
        g_object_unref(volume_);
    }
}

bool GstreamerAudioPlayer::Equalizer::setNumBands(int n) {
    if (!equalizer_) {
        return false;
    }

    g_object_set(G_OBJECT(equalizer_), "num-bands", n, nullptr);
    return true;
}

bool GstreamerAudioPlayer::Equalizer::setBandParams(int band, double freq, double width, double gain,
                                                    std::optional<YandexIO::EqualizerConfig::Band::Type> type)
{
    if (!equalizer_) {
        return false;
    }

    GObject* child = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(equalizer_), band);
    if (!child) {
        return false;
    }

    if (type.has_value()) {
        // according to  https://a.yandex-team.ru/arc_vcs/contrib/restricted/gst-plugins-good/gst/equalizer/gstiirequalizer.c?rev=a32a014adfd1ad68d61a1c8010e11a7b3fb15031#L80
        g_object_set(G_OBJECT(child), "type", type.value(), nullptr);
    }

    if (freq <= 0.0) {
        // Keep frequency and width
        g_object_set(
            G_OBJECT(child),
            "gain", gain,
            nullptr);
    } else {
        g_object_set(
            G_OBJECT(child),
            "freq", freq,
            "bandwidth", width,
            "gain", gain,
            nullptr);
    }

    g_object_unref(G_OBJECT(child));
    return true;
}

bool GstreamerAudioPlayer::Equalizer::setPreampGain(double gain) {
    if (!volume_) {
        return false;
    }

    double volume = pow(10.0, gain / 20.0);

    g_object_set(volume_, "volume", volume, nullptr);

    return true;
}

// Key methods explicitly defined to force class outlining, do not remove
GstreamerAudioPlayerFactory::GstreamerAudioPlayerFactory(std::shared_ptr<Gstreamer> gstreamer)
    : gstreamer_(std::move(gstreamer))
{
}
GstreamerAudioPlayerFactory::~GstreamerAudioPlayerFactory() = default;

std::unique_ptr<AudioPlayer> GstreamerAudioPlayerFactory::createPlayer(const AudioPlayer::Params& params) {
    std::unique_ptr<GstreamerAudioPlayer> result(new GstreamerAudioPlayer(gstreamer_, params));
    result->init(params.gstPipelineProcessed());
    return result;
}

void GstreamerAudioPlayerFactory::configUpdated(const Json::Value& serviceConfig) {
    const auto glibLoggingResolver = quasar::tryGetBool(serviceConfig, "glibLoggingResolver", DEFAULT_GLIB_LOGGING);
    YIO_LOG_INFO("Use glib logging resolver: " << glibLoggingResolver);
    gstreamer::GlibResolverSwitcher::instance().enableLoggingResolver(glibLoggingResolver);
}

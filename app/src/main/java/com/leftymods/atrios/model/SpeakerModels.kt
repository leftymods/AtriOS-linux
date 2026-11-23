package com.leftymods.atrios.model

import org.json.JSONArray
import org.json.JSONObject

data class DeviceCapabilities(
    val hasLedMatrix: Boolean = true,
    val hasRgbRing: Boolean = true,
    val hasLuxSensor: Boolean = true,
    val hasBluetoothSpeaker: Boolean = true,
    val hasLocalAi: Boolean = true
) {
    companion object {
        fun infer(codename: String, model: String, explicit: JSONObject? = null): DeviceCapabilities {
            val id = "${codename.lowercase()} ${model.lowercase()}"
            val defaultMatrix = when {
                id.contains("mini") && !id.contains("clock") -> false
                id.contains("lite") -> false
                else -> true
            }
            val defaultLux = !id.contains("lite")
            if (explicit == null) {
                return DeviceCapabilities(
                    hasLedMatrix = defaultMatrix,
                    hasRgbRing = true,
                    hasLuxSensor = defaultLux,
                    hasBluetoothSpeaker = true,
                    hasLocalAi = true
                )
            }
            return DeviceCapabilities(
                hasLedMatrix = explicit.optBoolean("led_matrix", defaultMatrix),
                hasRgbRing = explicit.optBoolean("rgb_ring", true),
                hasLuxSensor = explicit.optBoolean("lux_sensor", defaultLux),
                hasBluetoothSpeaker = explicit.optBoolean("bt_speaker", true),
                hasLocalAi = explicit.optBoolean("local_ai", true)
            )
        }
    }
}

data class SavedSpeaker(
    val mac: String,
    val name: String,
    val model: String = "AtriOS Speaker",
    val codename: String = "",
    val fwVersion: String = "",
    val lastSeenTs: Long = 0L
) {
    fun toJson(): JSONObject = JSONObject()
        .put("mac", mac)
        .put("name", name)
        .put("model", model)
        .put("codename", codename)
        .put("fw_version", fwVersion)
        .put("last_seen_ts", lastSeenTs)

    companion object {
        fun fromJson(obj: JSONObject): SavedSpeaker? {
            val mac = obj.optString("mac", "").trim()
            if (mac.isEmpty()) return null
            return SavedSpeaker(
                mac = mac,
                name = obj.optString("name", mac),
                model = obj.optString("model", "AtriOS Speaker"),
                codename = obj.optString("codename", ""),
                fwVersion = obj.optString("fw_version", ""),
                lastSeenTs = obj.optLong("last_seen_ts", 0L)
            )
        }
    }
}

data class OtaStatus(
    val state: String = "idle",
    val currentVersion: String = "",
    val latestVersion: String? = null,
    val latestTag: String? = null,
    val updateAvailable: Boolean = false,
    val progress: Int = 0,
    val stage: String = "idle",
    val message: String = "",
    val error: String? = null,
    val changelog: String = "",
    val assetSize: Long = 0L,
    val downloadedBytes: Long = 0L,
    val rollbackAvailable: Boolean = false,
    val lastCheckTs: Long = 0L,
    val updatedTs: Long = 0L,
    val rebootRequired: Boolean = false
) {
    val isBusy: Boolean
        get() = state in setOf("checking", "downloading", "verifying", "installing", "rolling_back")

    companion object {
        fun fromJson(obj: JSONObject?): OtaStatus {
            if (obj == null) return OtaStatus()
            return OtaStatus(
                state = obj.optString("state", "idle"),
                currentVersion = obj.optString("current_version", ""),
                latestVersion = if (obj.isNull("latest_version")) null else obj.optString("latest_version").ifBlank { null },
                latestTag = if (obj.isNull("latest_tag")) null else obj.optString("latest_tag").ifBlank { null },
                updateAvailable = obj.optBoolean("update_available", false),
                progress = obj.optInt("progress", 0).coerceIn(0, 100),
                stage = obj.optString("stage", "idle"),
                message = obj.optString("message", ""),
                error = if (obj.isNull("error")) null else obj.optString("error").ifBlank { null },
                changelog = obj.optString("changelog", ""),
                assetSize = obj.optLong("asset_size", 0L),
                downloadedBytes = obj.optLong("downloaded_bytes", 0L),
                rollbackAvailable = obj.optBoolean("rollback_available", false),
                lastCheckTs = obj.optLong("last_check_ts", 0L),
                updatedTs = obj.optLong("updated_ts", 0L),
                rebootRequired = obj.optBoolean("reboot_required", false)
            )
        }
    }
}

data class TimerItem(
    val id: Int,
    val fireAt: Long = 0L,
    val totalSecs: Int = 0,
    val remainingSecs: Int = 0
) {
    companion object {
        fun fromJsonArray(arr: JSONArray?): List<TimerItem> {
            if (arr == null) return emptyList()
            return buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    add(
                        TimerItem(
                            id = o.optInt("id", i + 1),
                            fireAt = o.optLong("fire_at", 0L),
                            totalSecs = o.optInt("total_secs", 0),
                            remainingSecs = o.optInt("remaining_secs", 0)
                        )
                    )
                }
            }
        }
    }
}

data class AlarmItem(
    val id: Int,
    val hour: Int,
    val minute: Int,
    val enabled: Boolean = true
) {
    val formattedTime: String
        get() = "%02d:%02d".format(hour.coerceIn(0, 23), minute.coerceIn(0, 59))

    companion object {
        fun fromJsonArray(arr: JSONArray?): List<AlarmItem> {
            if (arr == null) return emptyList()
            return buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    add(
                        AlarmItem(
                            id = o.optInt("id", i + 1),
                            hour = o.optInt("hour", 7),
                            minute = o.optInt("minute", 0),
                            enabled = o.optBoolean("enabled", true)
                        )
                    )
                }
            }
        }
    }
}

data class AiStatus(
    val enabled: Boolean = false,
    val mode: String = "hybrid",
    val server: String = "",
    val wakeWord: String = "Atri",
    val ttsVoice: String = "serena",
    val ttsEmotion: String = "neutral",
    val state: String = "disabled",
    val lastAsr: String = "",
    val lastAnswer: String = "",
    val error: String = ""
) {
    companion object {
        val MODES: List<Pair<String, String>> = listOf(
            "local" to "Локальный (офлайн)",
            "remote" to "GPU-сервер",
            "hybrid" to "Гибрид"
        )

        val QWEN3_VOICES: List<String> = listOf(
            "serena",
            "vivian",
            "aiden",
            "ryan",
            "sohee",
            "ono_anna",
            "dylan",
            "eric",
            "uncle_fu"
        )

        val QWEN3_EMOTIONS: List<Pair<String, String>> = listOf(
            "neutral" to "Нейтральный",
            "happy" to "Радостный",
            "sad" to "Грустный",
            "serious" to "Серьёзный",
            "whisper" to "Шёпот",
            "shout" to "Громко",
            "angry" to "Сердитый",
            "excited" to "Воодушевлённый",
            "surprised" to "Удивлённый"
        )

        fun fromJson(obj: JSONObject?): AiStatus {
            if (obj == null) return AiStatus()
            return AiStatus(
                enabled = obj.optBoolean("enabled", false),
                mode = obj.optString("mode", "hybrid").ifBlank { "hybrid" },
                server = obj.optString("server", ""),
                wakeWord = obj.optString("wake_word", "Atri"),
                ttsVoice = obj.optString("tts_voice", "serena").ifBlank { "serena" },
                ttsEmotion = obj.optString("tts_emotion", "neutral").ifBlank { "neutral" },
                state = obj.optString("state", "disabled"),
                lastAsr = obj.optString("last_asr", ""),
                lastAnswer = obj.optString("last_answer", ""),
                error = obj.optString("error", "")
            )
        }
    }
}


data class YMusicTrack(
    val id: String,
    val title: String,
    val artist: String = "",
    val album: String = "",
    val coverUrl: String = "",
    val durationMs: Long = 0L
) {
    companion object {
        fun fromJsonArray(arr: JSONArray?): List<YMusicTrack> {
            if (arr == null) return emptyList()
            return buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    val id = o.optString("id", "").trim()
                    val title = o.optString("title", "").trim()
                    if (id.isNotEmpty() && title.isNotEmpty()) {
                        add(
                            YMusicTrack(
                                id = id,
                                title = title,
                                artist = o.optString("artist", ""),
                                album = o.optString("album", ""),
                                coverUrl = o.optString("cover_url", ""),
                                durationMs = o.optLong("duration_ms", 0L)
                            )
                        )
                    }
                }
            }
        }
    }
}

data class YMusicAlbum(
    val id: String,
    val title: String,
    val artist: String = "",
    val year: Int = 0,
    val trackCount: Int = 0,
    val coverUrl: String = ""
) {
    companion object {
        fun fromJsonArray(arr: JSONArray?): List<YMusicAlbum> {
            if (arr == null) return emptyList()
            return buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    val id = o.optString("id", "").trim()
                    val title = o.optString("title", "").trim()
                    if (id.isNotEmpty() && title.isNotEmpty()) {
                        add(
                            YMusicAlbum(
                                id = id,
                                title = title,
                                artist = o.optString("artist", ""),
                                year = o.optInt("year", 0),
                                trackCount = o.optInt("track_count", 0),
                                coverUrl = o.optString("cover_url", "")
                            )
                        )
                    }
                }
            }
        }
    }
}


data class YMusicOrb(
    val id: String,
    val title: String,
    val subtitle: String = "",
    val mode: String = "wave",
    val query: String = "",
    val coverUrl: String = "",
    val colorHex: String = "#FF4D00"
) {
    companion object {
        fun fromJsonArray(arr: JSONArray?): List<YMusicOrb> {
            if (arr == null) return emptyList()
            return buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    val id = o.optString("id", "").trim()
                    val title = o.optString("title", "").trim()
                    if (id.isNotEmpty() && title.isNotEmpty()) {
                        add(
                            YMusicOrb(
                                id = id,
                                title = title,
                                subtitle = o.optString("subtitle", ""),
                                mode = o.optString("mode", "wave"),
                                query = o.optString("query", ""),
                                coverUrl = o.optString("cover_url", ""),
                                colorHex = o.optString("color", "#FF4D00")
                            )
                        )
                    }
                }
            }
        }
    }
}

data class YMusicStatus(
    val loggedIn: Boolean = false,
    val login: String = "",
    val displayName: String = "",
    val avatarUrl: String = "",
    val uid: Long = 0L,
    val state: String = "idle",
    val mode: String = "none",
    val trackId: String = "",
    val title: String = "",
    val artist: String = "",
    val album: String = "",
    val coverUrl: String = "",
    val nextTitle: String = "",
    val nextArtist: String = "",
    val nextCoverUrl: String = "",
    val prevTitle: String = "",
    val prevArtist: String = "",
    val prevCoverUrl: String = "",
    val durationMs: Long = 0L,
    val liked: Boolean = false,
    val error: String? = null,
    val audioBass: Float = 0f,
    val audioMid: Float = 0f,
    val audioEnergy: Float = 0f,
    val updatedTs: Long = 0L
) {
    val isActive: Boolean
        get() = state in setOf("loading", "playing", "paused")

    companion object {
        const val OAUTH_URL =
            "https://oauth.yandex.ru/authorize?response_type=token&client_id=23cabbbdc6cd418abb4b39c32c41195d"

        fun extractAccessToken(rawInput: String): String {
            val trimmed = rawInput.trim()
            if (trimmed.isEmpty()) return ""
            val match = Regex("(?:^|[#?&])access_token=([^&\\s]+)").find(trimmed)
            if (match != null) {
                return match.groupValues[1].trim()
            }
            return trimmed
        }

        fun fromJson(obj: JSONObject?): YMusicStatus {
            if (obj == null) return YMusicStatus()
            return YMusicStatus(
                loggedIn = obj.optBoolean("logged_in", false),
                login = obj.optString("login", ""),
                displayName = obj.optString("display_name", ""),
            avatarUrl = obj.optString("avatar_url", ""),
                uid = obj.optLong("uid", 0L),
                state = obj.optString("state", "idle").ifBlank { "idle" },
                mode = obj.optString("mode", "none").ifBlank { "none" },
                trackId = obj.optString("track_id", ""),
                title = obj.optString("title", ""),
                artist = obj.optString("artist", ""),
                album = obj.optString("album", ""),
            coverUrl = obj.optString("cover_url", ""),
                nextTitle = obj.optString("next_title", ""),
                nextArtist = obj.optString("next_artist", ""),
                nextCoverUrl = obj.optString("next_cover_url", ""),
                prevTitle = obj.optString("prev_title", ""),
                prevArtist = obj.optString("prev_artist", ""),
                prevCoverUrl = obj.optString("prev_cover_url", ""),
                durationMs = obj.optLong("duration_ms", 0L),
                liked = obj.optBoolean("liked", false),
                error = if (obj.isNull("error")) null else obj.optString("error").ifBlank { null },
                audioBass = obj.optDouble("audio_bass", 0.0).toFloat().coerceIn(0f, 1f),
                audioMid = obj.optDouble("audio_mid", 0.0).toFloat().coerceIn(0f, 1f),
                audioEnergy = obj.optDouble("audio_energy", 0.0).toFloat().coerceIn(0f, 1f),
                updatedTs = obj.optLong("updated_ts", 0L)
            )
        }
    }
}

data class SpeakerStatus(
    val model: String = "AtriOS Speaker",
    val codename: String = "",
    val rev: String = "",
    val soc: String = "",
    val arch: String = "",
    val os: String = "AtriOS",
    val fwVersion: String = "",
    val kernel: String = "",
    val localTime: String = "",
    val timezone: String = "UTC",
    val cpuTemp: Double = 0.0,
    val lux: Int? = null,
    val memUsedMb: Int = 0,
    val memTotalMb: Int = 0,
    val uptimeSec: Int = 0,
    val volume: Int = 60,
    val mute: Boolean = false,
    val speakersDesc: String = "",
    val micMuted: Boolean = false,
    val actionPressed: Boolean = false,
    val screenMode: String = "clock",
    val ringMode: String = "rainbow",
    val wifiConnected: Boolean = false,
    val wifiSsid: String = "",
    val wifiIp: String = "",
    val btSpeakerMode: Boolean = false,
    val btHidden: Boolean = false,
    val btDiscoverable: Boolean = false,
    val btPairRemainingSec: Int = 0,
    val pairedDevices: List<String> = emptyList(),
    val ai: AiStatus = AiStatus(),
    val ota: OtaStatus = OtaStatus(),
    val ymusic: YMusicStatus = YMusicStatus(),
    val capabilities: DeviceCapabilities = DeviceCapabilities()
)

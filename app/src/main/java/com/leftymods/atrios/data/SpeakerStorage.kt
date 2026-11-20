package com.leftymods.atrios.data

import android.content.Context
import com.leftymods.atrios.model.SavedSpeaker
import org.json.JSONArray

class SpeakerStorage(context: Context) {
    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun loadSpeakers(): List<SavedSpeaker> {
        val raw = prefs.getString(KEY_SPEAKERS, null) ?: return emptyList()
        return try {
            val arr = JSONArray(raw)
            buildList {
                for (i in 0 until arr.length()) {
                    val obj = arr.optJSONObject(i) ?: continue
                    SavedSpeaker.fromJson(obj)?.let { add(it) }
                }
            }.sortedByDescending { it.lastSeenTs }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun upsertSpeaker(speaker: SavedSpeaker): List<SavedSpeaker> {
        val current = loadSpeakers().filterNot { it.mac.equals(speaker.mac, ignoreCase = true) }
        val updated = (listOf(speaker) + current).sortedByDescending { it.lastSeenTs }
        saveAll(updated)
        return updated
    }

    fun removeSpeaker(mac: String): List<SavedSpeaker> {
        val updated = loadSpeakers().filterNot { it.mac.equals(mac, ignoreCase = true) }
        saveAll(updated)
        return updated
    }

    private fun saveAll(list: List<SavedSpeaker>) {
        val arr = JSONArray()
        list.forEach { arr.put(it.toJson()) }
        prefs.edit().putString(KEY_SPEAKERS, arr.toString()).apply()
    }

    companion object {
        private const val PREFS_NAME = "atrios_speakers"
        private const val KEY_SPEAKERS = "saved_speakers_v1"
    }
}

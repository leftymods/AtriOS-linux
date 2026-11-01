package org.atrios.companion.data.repository

import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor
import org.atrios.companion.data.api.AtriosApi
import org.atrios.companion.data.model.*
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.util.concurrent.TimeUnit

class StationRepository {
    private var currentHost: String = "192.168.1.100"
    private var api: AtriosApi = buildApi(currentHost)

    fun updateHost(host: String) {
        val cleanHost = host.trim().removePrefix("http://").removePrefix("https://").substringBefore(":")
        if (cleanHost.isNotEmpty() && cleanHost != currentHost) {
            currentHost = cleanHost
            api = buildApi(currentHost)
        }
    }

    fun getHost(): String = currentHost

    private fun buildApi(host: String): AtriosApi {
        val logging = HttpLoggingInterceptor().apply {
            level = HttpLoggingInterceptor.Level.BODY
        }
        val client = OkHttpClient.Builder()
            .connectTimeout(3, TimeUnit.SECONDS)
            .readTimeout(5, TimeUnit.SECONDS)
            .addInterceptor(logging)
            .build()

        return Retrofit.Builder()
            .baseUrl("http://$host:8080/")
            .client(client)
            .addConverterFactory(GsonConverterFactory.create())
            .build()
            .create(AtriosApi::class.java)
    }

    suspend fun fetchStatus(): Result<StationStatusResponse> = runCatching {
        val resp = api.getStatus()
        if (resp.isSuccessful && resp.body() != null) {
            resp.body()!!
        } else {
            throw Exception("HTTP ${resp.code()}: ${resp.message()}")
        }
    }

    suspend fun setVolume(volume: Int): Result<Unit> = runCatching {
        val resp = api.setVolume(VolumeRequest(volume))
        if (!resp.isSuccessful) throw Exception("Failed to set volume")
    }

    suspend fun setMute(mute: Boolean): Result<Unit> = runCatching {
        val resp = api.setMute(MuteRequest(mute))
        if (!resp.isSuccessful) throw Exception("Failed to set mute")
    }

    suspend fun testAudio(): Result<Unit> = runCatching {
        val resp = api.testAudio()
        if (!resp.isSuccessful) throw Exception("Failed to test audio")
    }

    suspend fun reboot(): Result<Unit> = runCatching {
        val resp = api.reboot()
        if (!resp.isSuccessful) throw Exception("Failed to reboot")
    }

    suspend fun scanWifi(): Result<List<AccessPointItem>> = runCatching {
        val resp = api.scanWifi()
        resp.body()?.accessPoints ?: emptyList()
    }
}

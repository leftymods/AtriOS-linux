package org.atrios.companion.data.api

import org.atrios.companion.data.model.*
import retrofit2.Response
import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.POST

interface AtriosApi {
    @GET("/api/status")
    suspend fun getStatus(): Response<StationStatusResponse>

    @POST("/api/volume")
    suspend fun setVolume(@Body request: VolumeRequest): Response<GenericResponse>

    @POST("/api/mute")
    suspend fun setMute(@Body request: MuteRequest): Response<GenericResponse>

    @POST("/api/audio/test")
    suspend fun testAudio(): Response<GenericResponse>

    @POST("/api/system/reboot")
    suspend fun reboot(): Response<GenericResponse>

    @GET("/api/wifi/scan")
    suspend fun scanWifi(): Response<WifiScanResponse>
}

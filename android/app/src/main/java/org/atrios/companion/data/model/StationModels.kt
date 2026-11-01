package org.atrios.companion.data.model

import com.google.gson.annotations.SerializedName

data class StationStatusResponse(
    @SerializedName("device") val device: DeviceInfo,
    @SerializedName("audio") val audio: AudioInfo,
    @SerializedName("wifi") val wifi: WifiInfo,
    @SerializedName("bluetooth") val bluetooth: BluetoothInfo,
    @SerializedName("zigbee") val zigbee: ZigbeeInfo
)

data class DeviceInfo(
    @SerializedName("model") val model: String,
    @SerializedName("codename") val codename: String,
    @SerializedName("soc") val soc: String,
    @SerializedName("os") val os: String,
    @SerializedName("kernel") val kernel: String,
    @SerializedName("uptime_seconds") val uptimeSeconds: Long,
    @SerializedName("cpu_temperature") val cpuTemperature: Double,
    @SerializedName("memory") val memory: MemoryInfo
)

data class MemoryInfo(
    @SerializedName("total_mb") val totalMb: Int,
    @SerializedName("free_mb") val freeMb: Int,
    @SerializedName("used_mb") val usedMb: Int
)

data class AudioInfo(
    @SerializedName("volume") val volume: Int,
    @SerializedName("mute") val mute: Boolean,
    @SerializedName("amplifier_pvdd") val amplifierPvdd: String,
    @SerializedName("card") val card: String,
    @SerializedName("speakers") val speakers: String
)

data class WifiInfo(
    @SerializedName("ip") val ip: String,
    @SerializedName("ssid") val ssid: String,
    @SerializedName("connected") val connected: Boolean
)

data class BluetoothInfo(
    @SerializedName("adapter") val adapter: String,
    @SerializedName("name") val name: String,
    @SerializedName("connected_devices") val connectedDevices: List<BluetoothDeviceItem> = emptyList(),
    @SerializedName("is_active") val isActive: Boolean = true,
    @SerializedName("is_speaker_mode") val isSpeakerMode: Boolean = false
)

data class BluetoothDeviceItem(
    @SerializedName("mac") val mac: String,
    @SerializedName("name") val name: String
)

data class ZigbeeInfo(
    @SerializedName("controller") val controller: String,
    @SerializedName("ready") val ready: Boolean
)

data class VolumeRequest(
    @SerializedName("volume") val volume: Int
)

data class MuteRequest(
    @SerializedName("mute") val mute: Boolean
)

data class GenericResponse(
    @SerializedName("success") val success: Boolean,
    @SerializedName("message") val message: String? = null
)

data class WifiScanResponse(
    @SerializedName("access_points") val accessPoints: List<AccessPointItem>
)

data class AccessPointItem(
    @SerializedName("ssid") val ssid: String,
    @SerializedName("signal_dbm") val signalDbm: Int
)

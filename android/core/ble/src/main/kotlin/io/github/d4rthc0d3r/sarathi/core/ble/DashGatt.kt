package io.github.d4rthc0d3r.sarathi.core.ble

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.util.UUID

/** The dash's GATT service (protocol.md section 1, ADR-0007). */
object DashGatt {
    val SERVICE: UUID = UUID.fromString("831fd13f-67ea-432e-b7b7-09926c4b92a6")
    val RX: UUID = UUID.fromString("cd32526c-d882-496f-b7c1-301a1fdf68a7") // phone -> dash
    val TX: UUID = UUID.fromString("9ae416fd-2b66-43f5-b902-001a23a238a9") // dash -> phone
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    const val WANTED_MTU = 517

    /** Whether the app may connect: Nearby devices (Android 12+); older versions grant it at install. */
    fun canConnect(context: Context): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
}

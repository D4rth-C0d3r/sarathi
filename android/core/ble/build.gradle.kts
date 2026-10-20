// BLE transport to the device (scan, bond, GATT). Android library; empty until Phase 4.

plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "io.github.d4rthc0d3r.sarathi.core.ble"
    compileSdk =
        libs.versions.compileSdk
            .get()
            .toInt()

    defaultConfig {
        minSdk =
            libs.versions.minSdk
                .get()
                .toInt()
    }

    compileOptions {
        sourceCompatibility = JavaVersion.toVersion(libs.versions.java.get())
        targetCompatibility = JavaVersion.toVersion(libs.versions.java.get())
    }
}

dependencies {
    implementation(project(":core:protocol"))
}

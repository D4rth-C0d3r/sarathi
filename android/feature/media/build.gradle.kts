// Music on the dash (Phase 4 step E): the phone's playing media session, track info and controls.
// Needs notification access (the listener lives in :app).

plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "io.github.d4rthc0d3r.sarathi.feature.media"
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
    implementation(project(":core:ble"))

    testImplementation(libs.junit)
}

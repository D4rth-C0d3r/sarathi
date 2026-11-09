// Turn-by-turn guidance (ADR-0006): Ferrostar for Mappls and OSM routes (and, in step F6.3, the
// Navigation SDK's own guidance for Google). The ride logic itself, reroutes included, is pure
// Kotlin in :core:maps (RideGuidance); this module adapts the engines to it.

plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "io.github.d4rthc0d3r.sarathi.feature.navigation"
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
    api(project(":core:maps"))
    implementation(libs.ferrostar.core)

    testImplementation(libs.junit)
}

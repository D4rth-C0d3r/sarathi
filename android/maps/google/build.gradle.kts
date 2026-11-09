// Google search (ADR-0006, ADR-0008): Places API (New) through the Places SDK. Routing with the
// Navigation SDK joins in step F4.

plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "io.github.d4rthc0d3r.sarathi.maps.google"
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
        // The Navigation SDK needs desugaring in every module that uses it (Google's setup guide).
        isCoreLibraryDesugaringEnabled = true
        sourceCompatibility = JavaVersion.toVersion(libs.versions.java.get())
        targetCompatibility = JavaVersion.toVersion(libs.versions.java.get())
    }
}

dependencies {
    api(project(":core:maps"))
    implementation(libs.google.places)
    implementation(libs.google.navigation)
    coreLibraryDesugaring(libs.desugar.jdk.libs.nio)
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
}

// The Navigation SDK bundles the Maps SDK; Places's own copy has to go (Google's setup guide).
configurations.configureEach {
    exclude(group = "com.google.android.gms", module = "play-services-maps")
}

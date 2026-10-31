// Mappls search (ADR-0006): its REST APIs through Mappls's SDK, which authenticates with the
// config files bound to the app's signature. Only the REST part, not the map SDK.

plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "io.github.d4rthc0d3r.sarathi.maps.mappls"
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
    implementation(libs.mappls.rest.apis)
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
}

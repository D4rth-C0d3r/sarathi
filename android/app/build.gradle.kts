plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// Keys are build-time secrets (ADR-0006, amended): files in ~/sarathi-secrets on the owner's
// laptop, never in the repository. A build without them (CI) still works; those backends then
// report "no key". The values are never printed.
val secretsDir = "${providers.systemProperty("user.home").get()}/sarathi-secrets"

fun secret(path: String): String =
    providers
        .fileContents(layout.projectDirectory.file("$secretsDir/$path"))
        .asText
        .orNull
        ?.trim()
        .orEmpty()

// Mappls's SDK config files (<appId>.a.conf and .a.olf, ADR-0006). Mappls's plugin turns them
// into resources, reading them from this module's root: they're copied there only while its task
// runs (and are gitignored as a backstop). Without them the plugin isn't applied.
val mapplsFiles =
    file("$secretsDir/mappls")
        .listFiles { f -> f.name.endsWith(".a.conf") || f.name.endsWith(".a.olf") }
        .orEmpty()
        .toList()
val mapplsConfigured = mapplsFiles.map { it.extension }.toSet() == setOf("conf", "olf")

// Offline routing tiles (tools/build_offline_tiles.sh, ADR-0006): packaged when they've been built,
// at assets/valhalla/. Without them the offline router reports "no data".
val offlineTiles = layout.projectDirectory.dir("offline")
val hasOfflineTiles = offlineTiles.file("valhalla/tiles.tar").asFile.exists()

android {
    namespace = "io.github.d4rthc0d3r.sarathi"
    compileSdk =
        libs.versions.compileSdk
            .get()
            .toInt()

    defaultConfig {
        applicationId = "io.github.d4rthc0d3r.sarathi"
        minSdk =
            libs.versions.minSdk
                .get()
                .toInt()
        targetSdk =
            libs.versions.targetSdk
                .get()
                .toInt()
        versionCode = 1
        versionName = "0.0.1"

        buildConfigField("String", "GOOGLE_API_KEY", "\"${secret("google/api-key")}\"")
        buildConfigField("String", "GRAPHHOPPER_API_KEY", "\"${secret("graphhopper/api-key")}\"")
        buildConfigField("boolean", "MAPPLS_CONFIGURED", mapplsConfigured.toString())
        // The Navigation SDK reads its key only from the manifest (no runtime setter).
        manifestPlaceholders["googleApiKey"] = secret("google/api-key")
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        // The Navigation SDK needs desugaring in the app too (Google's setup guide).
        isCoreLibraryDesugaringEnabled = true
        sourceCompatibility = JavaVersion.toVersion(libs.versions.java.get())
        targetCompatibility = JavaVersion.toVersion(libs.versions.java.get())
    }

    if (hasOfflineTiles) {
        sourceSets["main"].assets.directories += offlineTiles.dir("valhalla").asFile.parent
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    lint {
        abortOnError = true
        checkDependencies = true
    }
}

dependencies {
    implementation(project(":core:protocol"))
    implementation(project(":core:ble"))
    implementation(project(":core:maps"))
    implementation(project(":maps:google"))
    implementation(project(":maps:mappls"))
    implementation(project(":maps:osm"))
    implementation(project(":feature:destination"))
    implementation(libs.okhttp)
    coreLibraryDesugaring(libs.desugar.jdk.libs.nio)
    implementation(project(":feature:media"))
    implementation(project(":feature:calls"))

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.service)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    debugImplementation(libs.androidx.compose.ui.tooling)

    testImplementation(libs.junit)
}

// The Navigation SDK bundles the Maps SDK; Places's own copy has to go (Google's setup guide).
configurations.configureEach {
    exclude(group = "com.google.android.gms", module = "play-services-maps")
}

if (mapplsConfigured) {
    apply(plugin = "com.mappls.services.android")
    tasks.withType(com.mappls.services.android.MapplsServicesTask::class.java).configureEach {
        // Mappls's task reads Task.project while it runs, which the configuration cache forbids.
        notCompatibleWithConfigurationCache("Mappls's plugin uses Task.project at execution time")
        val sources = mapplsFiles
        val root = projectDir
        doFirst { sources.forEach { it.copyTo(root.resolve(it.name), overwrite = true) } }
        doLast { sources.forEach { root.resolve(it.name).delete() } }
    }
}

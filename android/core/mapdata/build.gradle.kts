// The phone's map pipeline (ADR-0009, Phase 4 step G2): vector tiles to the device's map chunks.
// Reads OpenMapTiles-schema tiles, keeps the road classes, water and parks of each level, clips,
// merges and simplifies in tile units (what the phone caches), then projects into a ride frame
// and quantizes. Pure Kotlin/JVM; tests match tools/make_chunks.py byte for byte on the recorded
// tiles in docs/fixtures/map/.

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.kotlin.jvm)
}

java {
    sourceCompatibility = JavaVersion.toVersion(libs.versions.java.get())
    targetCompatibility = JavaVersion.toVersion(libs.versions.java.get())
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.fromTarget(libs.versions.java.get()))
    }
}

dependencies {
    implementation(project(":core:protocol"))
    testImplementation(libs.junit)
}

// The tests read the recorded tiles and chunks in docs/fixtures/map; they rerun when those change.
val fixtures = layout.projectDirectory.dir("../../../docs/fixtures/map")

tasks.named<Test>("test") {
    inputs.dir(fixtures).withPathSensitivity(PathSensitivity.RELATIVE).withPropertyName("fixtures")
    systemProperty("sarathi.mapFixtures", fixtures.asFile.absolutePath)
}

// Lets `./gradlew testDebugUnitTest` (the Android modules' test task) run these tests too.
tasks.register("testDebugUnitTest") {
    dependsOn(tasks.named("test"))
}

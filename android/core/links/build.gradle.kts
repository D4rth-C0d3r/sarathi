// Shared links and the share sheet (ADR-0008): parses Google Maps, Mappls, OSM, Organic Maps and
// geo: links, decodes OSM and Organic Maps short codes offline, and follows short links by their
// redirects only. Pure Kotlin/JVM, tested against the links recorded on the owner's phone.

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
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.kotlinx.serialization.json)
}

// The tests read the recorded share links in docs/fixtures/; they rerun when those change.
val fixtures = layout.projectDirectory.file("../../../docs/fixtures/share-links.json")

tasks.named<Test>("test") {
    inputs.file(fixtures).withPathSensitivity(PathSensitivity.RELATIVE).withPropertyName("fixtures")
    systemProperty("sarathi.shareLinks", fixtures.asFile.absolutePath)
}

// Lets `./gradlew testDebugUnitTest` (the Android modules' test task) run these tests too.
tasks.register("testDebugUnitTest") {
    dependsOn(tasks.named("test"))
}

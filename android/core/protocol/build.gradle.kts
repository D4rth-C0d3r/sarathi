// Phone <-> device message encode/decode. Pure Kotlin/JVM (no Android), like the firmware's
// sarathi_proto, so it tests fast and runs against the shared golden vectors in
// docs/protocol/vectors/ (docs/protocol/protocol.md, ADR-0007). No dependencies.

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
}

// The tests read the golden vectors shared with the firmware; they rerun when those change.
val vectorsDir = layout.projectDirectory.dir("../../../docs/protocol/vectors")

tasks.named<Test>("test") {
    inputs.dir(vectorsDir).withPathSensitivity(PathSensitivity.RELATIVE).withPropertyName("vectors")
    systemProperty("sarathi.vectorsDir", vectorsDir.asFile.absolutePath)
}

// Lets `./gradlew testDebugUnitTest` (the Android modules' test task) run these tests too.
tasks.register("testDebugUnitTest") {
    dependsOn(tasks.named("test"))
}

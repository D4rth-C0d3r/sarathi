// Maps backends (ADR-0006): the provider interfaces and models, the registry and fallback
// chain, the cost guard, and the maps settings. Pure Kotlin/JVM, so it tests fast; the providers
// themselves live in :maps:google, :maps:mappls and :maps:osm.

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
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
    api(libs.kotlinx.coroutines.core)
    api(libs.kotlinx.serialization.json)

    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
}

// Lets `./gradlew testDebugUnitTest` (the Android modules' test task) run these tests too.
tasks.register("testDebugUnitTest") {
    dependsOn(tasks.named("test"))
}

// The OSM contribution helper (ADR-0012): the rider's raw GPS track, gap finding against OSM map
// data, and what goes to OpenStreetMap. Pure Kotlin/JVM.

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
    implementation(project(":core:mapdata"))
    testImplementation(libs.junit)
}

// ADR-0012: only the rider's own GPS and OSM data reach OpenStreetMap. This module may depend on
// these modules of the repo and no others, so Google's or Mappls's code and data can't reach it.
val allowedProjects = setOf(":core:protocol", ":core:mapdata")
configurations.configureEach {
    dependencies.withType<ProjectDependency>().configureEach {
        check(path in allowedProjects) {
            "$path can't be a dependency of :core:osmcontrib (ADR-0012): only $allowedProjects"
        }
    }
}

// Lets `./gradlew testDebugUnitTest` (the Android modules' test task) run these tests too.
tasks.register("testDebugUnitTest") {
    dependsOn(tasks.named("test"))
}

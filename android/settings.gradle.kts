pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
        // Mappls's SDK and its config-file plugin (ADR-0006); only its own groups come from here.
        maven("https://maven.mappls.com/repository/mappls/") {
            content { includeGroupByRegex("com\\.mappls.*") }
        }
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        maven("https://maven.mappls.com/repository/mappls/") {
            content { includeGroupByRegex("com\\.mappls.*") }
        }
    }
}

rootProject.name = "sarathi"

include(":app")
include(":core:protocol")
include(":core:ble")
include(":core:maps")
include(":maps:google")
include(":maps:mappls")
include(":maps:osm")
include(":feature:destination")
include(":feature:media")
include(":feature:calls")

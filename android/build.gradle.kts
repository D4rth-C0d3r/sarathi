import io.gitlab.arturbosch.detekt.extensions.DetektExtension
import org.jlleitschuh.gradle.ktlint.KtlintExtension

plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.jvm) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.ktlint) apply false
    alias(libs.plugins.detekt) apply false
}

// Style and static analysis for every module, including this root's build scripts.
// With a handful of modules this is simpler than convention plugins in build-logic/.
val ktlintVersion = libs.versions.ktlint.get()
val detektConfig = rootProject.file("config/detekt/detekt.yml")

allprojects {
    apply(plugin = "org.jlleitschuh.gradle.ktlint")
    apply(plugin = "io.gitlab.arturbosch.detekt")

    configure<KtlintExtension> {
        version.set(ktlintVersion)
    }

    configure<DetektExtension> {
        buildUponDefaultConfig = true
        config.setFrom(detektConfig)
    }
}

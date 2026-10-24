plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val appVersionCode = providers.gradleProperty("APP_VERSION_CODE").orElse("1").get().toInt()
val appVersionName = providers.gradleProperty("APP_VERSION_NAME").orElse("0.1.0").get()
val releaseKeystorePath = System.getenv("ANDROID_KEYSTORE_PATH")

android {
    namespace = "dev.droidcamclone.app"
    compileSdk = 36

    defaultConfig {
        applicationId = "dev.droidcamclone.app"
        minSdk = 23
        targetSdk = 36
        versionCode = appVersionCode
        versionName = appVersionName
    }

    signingConfigs {
        if (!releaseKeystorePath.isNullOrBlank()) {
            create("release") {
                storeFile = file(releaseKeystorePath)
                storePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("ANDROID_KEY_ALIAS")
                keyPassword = System.getenv("ANDROID_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (!releaseKeystorePath.isNullOrBlank()) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlin {
        compilerOptions {
            jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        }
    }
}

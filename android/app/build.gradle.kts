import java.util.Properties

plugins {
    id("com.android.application")
}

val repositoryRoot = rootDir.parentFile
val rootCMakeLists = repositoryRoot.resolve("CMakeLists.txt")
val projectVersion = Regex("""project\(OGPlay VERSION ([0-9.]+)""")
    .find(rootCMakeLists.readText())?.groupValues?.get(1)
    ?: throw GradleException("Cannot read the OGPlay version from $rootCMakeLists")
val stagedAssets = layout.buildDirectory.dir("generated/ogplay-assets").get().asFile
val releaseSigningPropertiesFile = rootProject.projectDir.resolve("keystore/release-signing.properties")
val releaseSigningProperties = Properties().apply {
    if (releaseSigningPropertiesFile.isFile) {
        releaseSigningPropertiesFile.inputStream().use(::load)
    }
}

android {
    namespace = "org.poping520.ogplay"
    compileSdk = 36
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "org.poping520.ogplay"
        minSdk = 29
        targetSdk = 36
        versionCode = 1
        versionName = projectVersion
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                // ogplaycore's C++ API crosses shared libraries, so the STL must be shared too.
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DOGPLAY_BUILD_TESTS=OFF",
                    "-DOGPLAY_RUNTIME_DATA_DIR=$stagedAssets/data",
                )
                targets += listOf("ogplay_android", "ogplay_launcher", "ogplay_stage_data")
            }
        }
    }

    if (releaseSigningPropertiesFile.isFile) {
        signingConfigs {
            create("localRelease") {
                storeFile = rootProject.projectDir.resolve(releaseSigningProperties.getProperty("storeFile"))
                storePassword = releaseSigningProperties.getProperty("storePassword")
                keyAlias = releaseSigningProperties.getProperty("keyAlias")
                keyPassword = releaseSigningProperties.getProperty("keyPassword")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = rootCMakeLists
            version = "4.1.2"
        }
    }

    buildTypes {
        release {
            if (releaseSigningPropertiesFile.isFile) {
                signingConfig = signingConfigs.getByName("localRelease")
            }
        }
        debug {
            externalNativeBuild {
                cmake {
                    // An unoptimized DexVM is too slow to judge a game on a device.
                    arguments += "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
                }
            }
        }
    }

    sourceSets.getByName("main") {
        java.directories += repositoryRoot.resolve("third_party/SDL/android-project/app/src/main/java").path
        assets.directories += stagedAssets.path
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// CMake stages the runtime data, so assets are merged only after the native build.
tasks.configureEach {
    Regex("merge(Debug|Release)Assets").matchEntire(name)?.let { match ->
        dependsOn("externalNativeBuild${match.groupValues[1]}")
    }
}

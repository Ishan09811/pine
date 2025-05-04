plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("kotlin-kapt")
    id("com.google.dagger.hilt.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("idea")
    id("org.jetbrains.kotlin.plugin.serialization") version "2.0.10"
}

idea {
    module {
        excludeDirs.add(file("libraries/boost"))
        excludeDirs.add(file("libraries/llvm"))
    }
}

android {
    namespace = "emu.skyline"
    compileSdk = 35

    val isBuildSigned = System.getenv("CI") == "true" && System.getenv("IS_BUILD_SIGNED") == "true"

    defaultConfig {
        applicationId = "io.github.pine.emu"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        manifestPlaceholders["emulationProcess"] = ""
        manifestPlaceholders.putAll(mapOf("emulationProcess" to ""))

        val locales = listOf("en", "de", "el", "es", "es-419", "fr", "hu", "id", "it", "ja", "ko", "pl", "ru", "ta", "zh-Hans", "zh-Hant")
        buildConfigField("String[]", "AVAILABLE_APP_LANGUAGES", "new String[]{\"${locales.joinToString("\",\"")}\"}")
    }

    kotlinOptions {
        jvmTarget = JavaVersion.VERSION_17.toString()
        freeCompilerArgs += "-opt-in=kotlin.RequiresOptIn"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packagingOptions {
        jniLibs.useLegacyPackaging = true
    }

    signingConfigs {
        create("ci") {
            storeFile = file(System.getenv("SIGNING_STORE_PATH"))
            storePassword = System.getenv("SIGNING_STORE_PASSWORD")
            keyAlias = System.getenv("SIGNING_KEY_ALIAS")
            keyPassword = System.getenv("SIGNING_KEY_PASSWORD")
        }
    }

    buildTypes {
        getByName("release") {
            isDebuggable = true
            externalNativeBuild {
                cmake {
                    arguments += listOf(
                        "-DCMAKE_BUILD_TYPE=RELEASE",
                        "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                    )
                }
            }
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = if (isBuildSigned) signingConfigs.getByName("ci") else signingConfigs.getByName("debug")
            manifestPlaceholders["emulationProcess"] = ":emulationProcess"
        }

        create("reldebug") {
            isDebuggable = true
            externalNativeBuild {
                cmake {
                    arguments += listOf(
                        "-DCMAKE_BUILD_TYPE=RELWITHDEBINFO",
                        "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                    )
                }
            }
            isMinifyEnabled = false
            isShrinkResources = false
            signingConfig = if (isBuildSigned) signingConfigs.getByName("ci") else signingConfigs.getByName("debug")
        }

        getByName("debug") {
            isDebuggable = true
            isMinifyEnabled = false
            isShrinkResources = false
            signingConfig = if (isBuildSigned) signingConfigs.getByName("ci") else signingConfigs.getByName("debug")
        }
    }

    flavorDimensions += "version"
    productFlavors {
        create("full") {
            dimension = "version"
            manifestPlaceholders["appLabel"] = "Pine"
        }

        create("dev") {
            dimension = "version"
            applicationIdSuffix = ".dev"
            versionNameSuffix = "-dev"
            manifestPlaceholders["appLabel"] = "Pine Dev"
        }
    }

    buildFeatures {
        viewBinding = true
        compose = true
        buildConfig = true
    }

    lint {
        disable += "IconLocation"
    }

    ndkVersion = "27.2.12479018"
    externalNativeBuild {
        cmake {
            version = "3.22.1+"
            path = file("CMakeLists.txt")
        }
    }

    androidResources {
        ignoreAssetsPattern = "*.md"
    }

    sourceSets {
        getByName("reldebug").jniLibs.srcDir("libraries/vklayers")
        getByName("debug").jniLibs.srcDir("libraries/vklayers")
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.preference:preference-ktx:1.2.1")
    implementation("androidx.activity:activity-ktx:1.8.2")
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation(platform("androidx.compose:compose-bom:2025.02.00"))
    implementation("com.google.android.material:material:1.10.0")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.navigation:navigation-compose:2.8.9")
    implementation("androidx.compose.ui:ui-tooling-preview-android:1.7.8")
    implementation("androidx.compose.ui:ui-tooling:1.7.8")
    implementation("androidx.documentfile:documentfile:1.0.1")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    implementation("androidx.window:window:1.2.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-ktx:2.6.2")
    implementation("androidx.lifecycle:lifecycle-livedata-ktx:2.6.2")
    implementation("androidx.fragment:fragment-ktx:1.6.2")
    implementation("com.google.dagger:hilt-android:2.50")
    kapt("com.google.dagger:hilt-compiler:2.50")
    implementation("com.google.android.flexbox:flexbox:3.0.0")
    implementation("androidx.palette:palette-ktx:1.0.0")
    implementation("org.jetbrains.kotlin:kotlin-reflect:2.0.10")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.2")
    implementation("org.jetbrains.kotlin:kotlin-stdlib:2.0.10")
    implementation("info.debatty:java-string-similarity:2.0.0")
    implementation("com.github.KikiManjaro:colorpicker:v1.1.12")
    implementation("com.github.android:renderscript-intrinsics-replacement-toolkit:344be3f")
    implementation("io.ktor:ktor-client-core:3.0.3")
    implementation("io.ktor:ktor-client-cio:3.0.3")
    implementation("io.ktor:ktor-client-json:3.0.3")
    implementation("io.ktor:ktor-serialization-kotlinx-json:3.0.3")
    implementation("io.ktor:ktor-client-content-negotiation:3.0.3")
    implementation("io.ktor:ktor-client-logging:3.0.3")
}

kapt {
    correctErrorTypes = true
}

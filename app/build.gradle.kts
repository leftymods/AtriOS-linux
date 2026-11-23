plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.leftymods.atrios"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.leftymods.atrios"
        minSdk = 26
        targetSdk = 34
        versionCode = 18
        versionName = "1.17"
    }

    signingConfigs {
        getByName("debug") {
            storeFile = file("atrios-signing.jks")
            storePassword = "atrios2026"
            keyAlias = "atrios"
            keyPassword = "atrios2026"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.09.02"))
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
}

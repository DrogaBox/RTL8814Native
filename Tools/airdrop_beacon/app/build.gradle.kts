plugins {
    id("com.android.application")
}

android {
    namespace = "com.drogabox.airdropbeacon"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.drogabox.airdropbeacon"
        minSdk = 31
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

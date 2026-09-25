pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "MobileGLPlugin"
include(":app", ":MobileGL")
project(":MobileGL").projectDir = file("..")
// mg-3backends: vendored MobileGlues core (Air 6.0 "GLES" backend)
include(":mg-air")
project(":mg-air").projectDir = file("../mg-air")

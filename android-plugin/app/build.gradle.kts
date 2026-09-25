import com.android.build.gradle.internal.api.ApkVariantOutputImpl
import com.launchers_plugin.renderer.buildscript.RendererConfig
import com.launchers_plugin.renderer.buildscript.buildEnvs
import com.launchers_plugin.renderer.buildscript.buildJsonValue
import com.launchers_plugin.renderer.buildscript.legacyManifest
import com.launchers_plugin.renderer.buildscript.nativePath
import com.launchers_plugin.renderer.buildscript.renderer

buildscript {
    repositories {
        maven("https://jitpack.io")
    }
    dependencies {
        classpath("com.github.ZalithLauncher.RendererPlugin-v2:dsl:1.0-alpha6")
    }
}

plugins {
    id("com.android.application")
}

apply(plugin = "com.launchers_plugin.renderer.dsl")

fun Project.mobileGlAbiFilters(): List<String> {
    val abiList = (findProperty("mobilegl.abis") ?: System.getenv("MOBILEGL_ABIS") ?: "arm64-v8a").toString()
    return if (abiList.equals("all", ignoreCase = true)) {
        listOf("arm64-v8a", "x86_64")
    } else {
        abiList.split(',').map(String::trim).filter(String::isNotEmpty)
    }
}

fun Project.mobileGlCmakeCompilerLauncher(): String =
    (findProperty("mobilegl.cmakeCompilerLauncher") ?: System.getenv("MOBILEGL_CMAKE_COMPILER_LAUNCHER") ?: "").toString().trim()

// Optional application-id suffix, so a development build can sit next to an
// already-installed plugin instead of having to replace it - a differently
// signed APK cannot upgrade one in place, and uninstalling costs the user their
// plugin settings and the launcher's binding to it.
fun Project.mobileGlApplicationIdSuffix(): String =
    (findProperty("mobilegl.applicationIdSuffix") ?: "").toString().trim()

fun Project.runGit(vararg arguments: String): String? = runCatching {
    ProcessBuilder("git", *arguments)
        .directory(rootDir)
        .redirectErrorStream(true)
        .start()
        .let { process ->
            val output = process.inputStream.bufferedReader().use { it.readText().trim() }
            output.takeIf { process.waitFor() == 0 && it.isNotEmpty() }
        }
}.getOrNull()

val signingStoreFile = file("../keystore-air.p12")
// mg-3backends: the fork signs with its own committed PKCS12 keystore so
// releases stay installable (stable signature across runs) without access to
// the upstream CI secrets. CI passes the SIGNING_* secrets as EMPTY strings
// on forks, so blank counts as absent here and the committed-keystore
// fallback kicks in. Env overrides remain for fork-side rotation.
fun signingEnvOr(name: String, fallback: String): String =
    System.getenv(name)?.takeIf { it.isNotBlank() } ?: fallback
val signingStorePassword = signingEnvOr("SIGNING_STORE_PASSWORD", "mgair-air-3backends")
val signingKeyAlias = signingEnvOr("SIGNING_KEY_ALIAS", "mgair")
val signingKeyPassword = signingEnvOr("SIGNING_KEY_PASSWORD", "mgair-air-3backends")
val releaseSigningReady = signingStoreFile.exists()
    && !signingStorePassword.isNullOrEmpty()
    && !signingKeyAlias.isNullOrEmpty()
    && !signingKeyPassword.isNullOrEmpty()
val debuggableRelease = (findProperty("mobilegl.debuggableRelease") ?: "false").toString().toBoolean()

val mobileGlVersionMajor = 26
val mobileGlVersionMinor = 8
val mobileGlGitShortHash = runGit("rev-parse", "--short=7", "HEAD") ?: "nogit"
val mobileGlMonthlyRevision = runGit(
    "rev-list",
    "--count",
    "--since=${String.format("%d-%02d-01T00:00:00", 2000 + mobileGlVersionMajor, mobileGlVersionMinor)}",
    "HEAD",
)?.toIntOrNull() ?: 0
val mobileGlApkSuffix = (findProperty("mobilegl.apkSuffix") ?: System.getenv("MOBILEGL_APK_SUFFIX") ?: mobileGlGitShortHash)
    .toString()
    .ifBlank { "nogit" }

val pluginRendererConfig = buildJsonValue {
    renderer(
        displayName = "MobileGL",
        rendererId = "opengles3",
        rendererGLPath = nativePath("libmobileglues.so"),
        rendererEGLPath = nativePath("libmobileglues.so"),
        // mg-3backends: the launcher must also be able to extract/dlopen the
        // backend cores the dispatcher routes to.
        dlopenLibPaths = listOf("libMobileGL.so", "libmg_gles.so"),
        env = buildEnvs {
            normal("LIBGL_ES", "3")
            selectable(
                key = "MOBILEGL_BACKEND_TYPE",
                title = RendererConfig.MetaString("mobilegl_backend_type_title"),
                // mg-3backends (Air 6.0 definition): one "mg" entry, three
                // backends selectable in the launcher settings. Default is
                // Air 6.0's: Vulkan direct. DirectGLES resolves inside the
                // MobileGL core; "MobileGlues" routes to the vendored
                // libmg_gles.so (GL -> OpenGL ES, FSR1-capable).
                items = RendererConfig.EnvItems("DirectVulkan", listOf("MobileGlues", "DirectGLES")),
            )
            // mg-3backends: FSR1 quality tier for the backends that ship it
            // (GLES core native EASU+RCAS; others land in follow-up builds).
            // 0=off 1=UltraQuality 2=Quality 3=Balanced 4=Performance
            customizable("MOBILEGL_FSR1", "0", RendererConfig.MetaString("mobilegl_fsr1_title"))
            toggleable("MOBILEGL_DISABLE_TIMERQUERY", "1", false, RendererConfig.MetaString("mobilegl_disable_timerquery_title"))
            toggleable("MOBILEGL_MAGMA_DISABLE_SUBGROUP", "1", false, RendererConfig.MetaString("mobilegl_disable_subgroup_title"))
            toggleable("MOBILEGL_MAGMA_R11G11B10F_FALLBACK", "1", false, RendererConfig.MetaString("mobilegl_magma_r11g11b10f_fallback_title"))
            customizable("MOBILEGL_MAGMA_FRAMESINFLIGHT", "3", RendererConfig.MetaString("mobilegl_magma_frames_inflight_title"))
            toggleable("MOBILEGL_ESPRYT_AVOID_SAMPLER_MIPMAP_MIN_FILTER", "1", false, RendererConfig.MetaString("mobilegl_avoid_sampler_mipmap_min_filter_title"))
            toggleable("MOBILEGL_COHERENT_AS_FLUSH", "1", false, RendererConfig.MetaString("mobilegl_coherent_as_flush_title"))
            toggleable("MOBILEGL_RELAXED_SEMANTICS", "1", false, RendererConfig.MetaString("mobilegl_relaxed_semantics_title"))
            toggleable("MOBILEGL_ESPRYT_USE_ANGLE", "1", false, RendererConfig.MetaString("mobilegl_use_angle_title"))
        },
        minMCVer = null,
        maxMCVer = null,
    )
}

android {
    namespace = "top.mobilegl.plugin"
    compileSdk = 34
    ndkVersion = "27.3.13750724"

    defaultConfig {
        applicationId = "top.mobilegl.plugin"
        mobileGlApplicationIdSuffix().takeIf { it.isNotEmpty() }?.let { applicationIdSuffix = it }
        minSdk = 26
        targetSdk = 34
        versionCode = mobileGlVersionMajor * 1_000_000 + mobileGlVersionMinor * 10_000 + mobileGlMonthlyRevision
        versionName = "%d.%02d.%s".format(mobileGlVersionMajor, mobileGlVersionMinor, mobileGlGitShortHash)
        resValue("string", "config", pluginRendererConfig)

        manifestPlaceholders.putAll(legacyManifest {
            displayName = "MobileGL"
            rendererName = "MobileGL"
            // mg-3backends: legacy launchers load the unified dispatcher; it
            // routes to libMobileGL.so / libmg_gles.so by backend selection.
            rendererLib = "libmobileglues.so"
            eglLib = "/libmobileglues.so"
            minMCVer = ""
            maxMCVer = ""
            boatEnv {
                put("LIBGL_ES", "3")
                put("POJAV_RENDERER", "opengles3")
                // Air 6.0 default: Vulkan direct
                put("MOBILEGL_BACKEND_TYPE", "DirectVulkan")
            }
            pojavEnv {
                put("LIBGL_ES", "3")
                put("POJAV_RENDERER", "opengles3")
                put("POJAVEXEC_EGL", "libmobileglues.so")
                put("LIBGL_EGL", "libmobileglues.so")
                // Air 6.0 default: Vulkan direct
                put("MOBILEGL_BACKEND_TYPE", "DirectVulkan")
            }
        })
        manifestPlaceholders["appLabel"] = "MobileGL"

        ndk {
            abiFilters += mobileGlAbiFilters()
        }
        externalNativeBuild {
            cmake {
                mobileGlCmakeCompilerLauncher().takeIf(String::isNotEmpty)?.let { compilerLauncher ->
                    arguments += listOf(
                        "-DCMAKE_C_COMPILER_LAUNCHER=$compilerLauncher",
                        "-DCMAKE_CXX_COMPILER_LAUNCHER=$compilerLauncher",
                    )
                }
            }
        }
    }

    buildFeatures {
        resValues = true
    }

    flavorDimensions += "profile"
    productFlavors {
        create("plugin") {
            dimension = "profile"
        }
        create("trace") {
            dimension = "profile"
            applicationIdSuffix = ".trace"
            versionNameSuffix = "-trace"
        }
    }

    if (releaseSigningReady) {
        signingConfigs {
            create("release") {
                storeFile = signingStoreFile
                storeType = "PKCS12"
                storePassword = signingStorePassword
                keyAlias = signingKeyAlias
                keyPassword = signingKeyPassword
            }
        }
    }

    buildTypes {
        getByName("release") {
            isDebuggable = debuggableRelease
            isMinifyEnabled = false
            if (releaseSigningReady) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/trace/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
            excludes += "**/libSPIRV-Tools-shared.so"
        }
    }
}

android.applicationVariants.configureEach {
    outputs.configureEach {
        (this as ApkVariantOutputImpl).outputFileName = when (flavorName) {
            "plugin" -> "MobileGL-plugin-release-$mobileGlApkSuffix.apk"
            "trace" -> "MobileGL-plugin-trace-release-$mobileGlApkSuffix.apk"
            else -> outputFileName
        }
    }
}

androidComponents {
    onVariants(selector().withFlavor("profile" to "plugin")) { variant ->
        variant.packaging.jniLibs.excludes.add("**/libtrace_replay_runner.so")
    }
}

dependencies {
    implementation(project(":MobileGL"))
    // mg-3backends: vendored MobileGlues core (libmg_gles.so)
    implementation(project(":mg-air"))
}

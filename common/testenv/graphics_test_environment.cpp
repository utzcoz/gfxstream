// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gfxstream/common/testing/graphics_test_environment.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#if defined(__APPLE__)
#include <unistd.h>
#endif

#ifdef BAZEL_CURRENT_REPOSITORY
#include <rules_cc/cc/runfiles/runfiles.h>
#endif

#include "gfxstream/common/logging.h"
#include "gfxstream/system/System.h"

namespace gfxstream {
namespace testing {
namespace {

#if defined(GFXSTREAM_TESTING_USE_GLES_ANGLE)
std::optional<std::filesystem::path> GetGraphicsDriverPath(const std::string& basename) {
#if defined(BAZEL_CURRENT_REPOSITORY)
    // https://github.com/bazelbuild/rules_cc/blob/main/cc/runfiles/runfiles.h
    using rules_cc::cc::runfiles::Runfiles;
    static Runfiles* sRunfiles = []() -> Runfiles* {
        std::string error;
        auto* runfiles = Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error);
        if (runfiles == nullptr) {
            GFXSTREAM_ERROR("Failed to load runfiles: %s.", error.c_str());
            return nullptr;
        }
        return runfiles;
    }();
    if (sRunfiles == nullptr) {
        GFXSTREAM_ERROR("Testdata runfiles not available?");
        return std::nullopt;
    }
    const std::vector<std::string> possiblePaths = {
        std::string("_main/common/testenv/graphics_test_environment_drivers/") + basename,
    };
    for (const std::string& possiblePath : possiblePaths) {
        const std::string path = sRunfiles->Rlocation(possiblePath);
        if (!path.empty() && std::filesystem::exists(path)) {
            return path;
        }
    }
    GFXSTREAM_ERROR("Failed to find %s in graphics test environment data files.", basename.c_str());
    return std::nullopt;
#else
    GFXSTREAM_ERROR("Library built without BAZEL_CURRENT_REPOSITORY?");
    return std::nullopt;
#endif  // defined(BAZEL_CURRENT_REPOSITORY)
}
#endif  // defined(GFXSTREAM_TESTING_USE_GLES_ANGLE)

}  // namespace

bool SetupGraphicsTestEnvironment() {
#if defined(GFXSTREAM_TESTING_USE_GLES_ANGLE)
    GFXSTREAM_INFO("GraphicsTestEnvironment: configuring ANGLE as EGL/GLES driver.");

    // TODO: Update ANGLE build to support running with GLVND. See
    // https://github.com/NVIDIA/libglvnd/blob/master/include/glvnd/libeglabi.h.
    // Then uncomment:
    //
    //     const auto driverGlesOpt = GetGraphicsDriverPath("libGLESv2_angle.so.2");
    //     if (!driverGlesOpt) {
    //         GFXSTREAM_ERROR("Failed to find libGLESv2_angle.so.2");
    //         return false;
    //     }
    //     const auto driverEglOpt = GetGraphicsDriverPath("libEGL_angle.so.1");
    //     if (!driverEglOpt) {
    //         GFXSTREAM_ERROR("Failed to find libEGL_angle.so.1");
    //         return false;
    //     }
    //     const auto driverEglIcdOpt = GetGraphicsDriverPath("libEGL_angle_vendor_icd.json");
    //     if (!driverEglOpt) {
    //         GFXSTREAM_ERROR("Failed to find libEGL_angle_vendor_icd.json");
    //         return false;
    //     }
    //     const std::string driverEglIcd = driverEglIcdOpt->string();
    //     gfxstream::base::setEnvironmentVariable("__EGL_VENDOR_LIBRARY_FILENAMES", driverEglIcd);
    //
    // For now, assume the ANGLE libs are directly used:
#if defined(__APPLE__)
    static constexpr const char* kGlesDriverBasename = "libGLESv2.dylib";
    static constexpr const char* kEglDriverBasename = "libEGL.dylib";
#else
    static constexpr const char* kGlesDriverBasename = "libGLESv2.so";
    static constexpr const char* kEglDriverBasename = "libEGL.so";
#endif
    const auto driverGlesOpt = GetGraphicsDriverPath(kGlesDriverBasename);
    if (!driverGlesOpt) {
        GFXSTREAM_ERROR("Failed to find %s.", kGlesDriverBasename);
        return false;
    }
    const auto driverEglOpt = GetGraphicsDriverPath(kEglDriverBasename);
    if (!driverEglOpt) {
        GFXSTREAM_ERROR("Failed to find %s", kEglDriverBasename);
        return false;
    }
    const std::filesystem::path driverEgl = *driverEglOpt;
    const std::filesystem::path driverDirectory = driverEgl.parent_path();

    const std::string currentLdLibraryPath = gfxstream::base::getEnvironmentVariable("LD_LIBRARY_PATH");
    const std::string updatedLdLibraryPath = driverDirectory.string() + ":" + currentLdLibraryPath;
    gfxstream::base::setEnvironmentVariable("LD_LIBRARY_PATH", updatedLdLibraryPath);

#if defined(GFXSTREAM_TESTING_USE_VULKAN_MOLTENVK)
    // ANGLE searches its own module directory for the Vulkan loader, and
    // DYLD_LIBRARY_PATH is unavailable under macOS System Integrity Protection.
    // The Bazel output directory holding the ANGLE libraries is read-only, so
    // stage the ANGLE libraries and the system Vulkan loader together in a
    // writable directory and load ANGLE from there. The loader path and the
    // MoltenVK ICD are provided by the caller (see CI).
    const std::string vulkanLoaderSource =
        gfxstream::base::getEnvironmentVariable("GFXSTREAM_TESTING_VULKAN_LOADER");
    if (vulkanLoaderSource.empty()) {
        GFXSTREAM_ERROR("GFXSTREAM_TESTING_VULKAN_LOADER is not set for the MoltenVK environment.");
        return false;
    }
    std::error_code stagingError;
    const std::filesystem::path temporaryDirectory =
        std::filesystem::temp_directory_path(stagingError);
    if (stagingError) {
        GFXSTREAM_ERROR("Failed to find a temporary directory: %s.",
                        stagingError.message().c_str());
        return false;
    }
    const std::filesystem::path stagingDirectory =
        temporaryDirectory / ("gfxstream_moltenvk_drivers_" + std::to_string(getpid()));
    std::filesystem::create_directories(stagingDirectory, stagingError);
    if (stagingError) {
        GFXSTREAM_ERROR("Failed to create staging directory %s: %s.",
                        stagingDirectory.string().c_str(), stagingError.message().c_str());
        return false;
    }
    // The staged copies inherit the read-only permissions of their Bazel source,
    // so a later call (SetupGraphicsTestEnvironment runs once per test) cannot
    // truncate them to overwrite. Remove any existing destination first, which
    // also keeps staging idempotent across tests in the same process.
    const auto stageInto = [&](const std::filesystem::path& source,
                               const std::filesystem::path& destination) -> bool {
        std::error_code removeError;
        std::filesystem::remove(destination, removeError);
        std::error_code copyError;
        std::filesystem::copy_file(source, destination,
                                   std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError) {
            GFXSTREAM_ERROR("Failed to stage %s into %s: %s.", source.string().c_str(),
                            destination.string().c_str(), copyError.message().c_str());
            return false;
        }
        return true;
    };
    const auto stageFile = [&](const std::filesystem::path& source) -> bool {
        return stageInto(source, stagingDirectory / source.filename());
    };
    if (!stageFile(*driverGlesOpt) || !stageFile(driverEgl)) {
        return false;
    }
    // Stage the loader under the name ANGLE looks for.
    if (!stageInto(vulkanLoaderSource, stagingDirectory / "libvulkan.dylib")) {
        return false;
    }
    gfxstream::base::setEnvironmentVariable(
        "LD_LIBRARY_PATH", stagingDirectory.string() + ":" + updatedLdLibraryPath);
#endif  // defined(GFXSTREAM_TESTING_USE_VULKAN_MOLTENVK)
#else
    GFXSTREAM_INFO("GraphicsTestEnvironment: not changing host EGL/GLES driver configuration.");
#endif  // defined(GFXSTREAM_TESTING_USE_GLES_ANGLE)

#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE) || defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
    GFXSTREAM_INFO("GraphicsTestEnvironment: configuring locally built Vulkan driver.");

    const std::string driverBasename =
#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
        "libvk_lavapipe.so";
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
        "libvk_swiftshader.so";
#else
#error "Supported host vulkan driver in GraphicsTestEnvironment!"
#endif

    const std::string driverIcdBasename =
#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
        "vk_lavapipe_icd.json";
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
        "vk_swiftshader_icd.json";
#else
#error "Supported host vulkan driver in GraphicsTestEnvironment!"
#endif

    const auto driverLavapipeOpt = GetGraphicsDriverPath(driverBasename);
    if (!driverLavapipeOpt) {
        GFXSTREAM_ERROR("Failed to find %s", driverBasename.c_str());
        return false;
    }
    const auto driverLavapipeIcdOpt = GetGraphicsDriverPath(driverIcdBasename);
    if (!driverLavapipeIcdOpt) {
        GFXSTREAM_ERROR("Failed to find %s", driverIcdBasename.c_str());
        return false;
    }
    const std::string driverLavapipeIcd = driverLavapipeIcdOpt->string();
    gfxstream::base::setEnvironmentVariable("VK_DRIVER_FILES", driverLavapipeIcd);
    gfxstream::base::setEnvironmentVariable("VK_ICD_FILENAMES", driverLavapipeIcd);
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_MOLTENVK)
    GFXSTREAM_INFO("GraphicsTestEnvironment: configuring MoltenVK as the Vulkan driver.");

    // MoltenVK is provided by the system (e.g. Homebrew), not as a build
    // artifact. The caller provides the ICD via GFXSTREAM_TESTING_VULKAN_ICD.
    const std::string moltenVkIcd =
        gfxstream::base::getEnvironmentVariable("GFXSTREAM_TESTING_VULKAN_ICD");
    if (moltenVkIcd.empty()) {
        GFXSTREAM_ERROR("GFXSTREAM_TESTING_VULKAN_ICD is not set for the MoltenVK environment.");
        return false;
    }
    gfxstream::base::setEnvironmentVariable("VK_DRIVER_FILES", moltenVkIcd);
    gfxstream::base::setEnvironmentVariable("VK_ICD_FILENAMES", moltenVkIcd);
#else
    GFXSTREAM_INFO("GraphicsTestEnvironment: not changing host Vulkan driver configuration.");
#endif  // defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE) || defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)

    return true;
}

bool IsGraphicsTestEnvironmentProvidingVulkanDriver() {
#if defined(GFXSTREAM_TESTING_USE_VULKAN_LAVAPIPE)
    return true;
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_SWIFTSHADER)
    return true;
#elif defined(GFXSTREAM_TESTING_USE_VULKAN_MOLTENVK)
    return true;
#else
    return false;
#endif
}

}  // namespace testing
}  // namespace gfxstream

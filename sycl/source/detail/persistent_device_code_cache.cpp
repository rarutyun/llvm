//==---------- persistent_device_code_cache.cpp -----------------*- C++-*---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <detail/device_impl.hpp>
#include <detail/persistent_device_code_cache.hpp>
#include <detail/plugin.hpp>
#include <detail/program_manager/program_manager.hpp>

#include <cstdio>
#include <fstream>
#include <optional>

#if defined(__SYCL_RT_OS_POSIX_SUPPORT)
#include <unistd.h>
#else
#include <direct.h>
#include <io.h>
#endif

namespace sycl {
inline namespace _V1 {
namespace detail {

/* Lock file suffix */
const char LockCacheItem::LockSuffix[] = ".lock";

LockCacheItem::LockCacheItem(const std::string &Path)
    : FileName(Path + LockSuffix) {
  int fd;

  /* If the lock fail is not created */
  if ((fd = open(FileName.c_str(), O_CREAT | O_EXCL, S_IWRITE)) != -1) {
    close(fd);
    Owned = true;
  } else {
    PersistentDeviceCodeCache::trace("Failed to aquire lock file: " + FileName);
  }
}

LockCacheItem::~LockCacheItem() {
  if (Owned && std::remove(FileName.c_str()))
    PersistentDeviceCodeCache::trace("Failed to release lock file: " +
                                     FileName);
}

// Returns true if the specified format is either SPIRV or a native binary.
static bool
IsSupportedImageFormat(sycl::detail::pi::PiDeviceBinaryType Format) {
  return Format == PI_DEVICE_BINARY_TYPE_SPIRV ||
         Format == PI_DEVICE_BINARY_TYPE_NATIVE;
}

/* Returns true if specified image should be cached on disk. It checks if
 * cache is enabled, image has supported format and matches thresholds. */
bool PersistentDeviceCodeCache::isImageCached(const RTDeviceBinaryImage &Img) {
  // Cache should be enabled and image type is one of the supported formats.
  if (!isEnabled() || !IsSupportedImageFormat(Img.getFormat()))
    return false;

  // Disable cache for ITT-profiled images.
  if (SYCLConfig<INTEL_ENABLE_OFFLOAD_ANNOTATIONS>::get()) {
    return false;
  }

  // TODO: Move parsing logic and caching to specializations of SYCLConfig.
  static auto MaxImgSize = getNumParam<SYCL_CACHE_MAX_DEVICE_IMAGE_SIZE>(
      DEFAULT_MAX_DEVICE_IMAGE_SIZE);
  static auto MinImgSize = getNumParam<SYCL_CACHE_MIN_DEVICE_IMAGE_SIZE>(
      DEFAULT_MIN_DEVICE_IMAGE_SIZE);

  // Make sure that image size is between caching thresholds if they are set.
  // Zero values for threshold is treated as disabled threshold.
  if ((MaxImgSize && (Img.getSize() > MaxImgSize)) ||
      (MinImgSize && (Img.getSize() < MinImgSize)))
    return false;

  return true;
}

/* Stores built program in persisten cache
 */
void PersistentDeviceCodeCache::putItemToDisc(
    const device &Device, const RTDeviceBinaryImage &Img,
    const SerializedObj &SpecConsts, const std::string &BuildOptionsString,
    const sycl::detail::pi::PiProgram &NativePrg) {

  if (!isImageCached(Img))
    return;

  std::string DirName =
      getCacheItemPath(Device, Img, SpecConsts, BuildOptionsString);

  if (DirName.empty())
    return;

  auto Plugin = detail::getSyclObjImpl(Device)->getPlugin();

  size_t i = 0;
  std::string FileName;
  do {
    FileName = DirName + "/" + std::to_string(i++);
  } while (OSUtil::isPathPresent(FileName + ".bin"));

  unsigned int DeviceNum = 0;

  Plugin->call<PiApiKind::piProgramGetInfo>(
      NativePrg, PI_PROGRAM_INFO_NUM_DEVICES, sizeof(DeviceNum), &DeviceNum,
      nullptr);

  std::vector<size_t> BinarySizes(DeviceNum);
  Plugin->call<PiApiKind::piProgramGetInfo>(
      NativePrg, PI_PROGRAM_INFO_BINARY_SIZES,
      sizeof(size_t) * BinarySizes.size(), BinarySizes.data(), nullptr);

  std::vector<std::vector<char>> Result;
  std::vector<char *> Pointers;
  for (size_t I = 0; I < BinarySizes.size(); ++I) {
    Result.emplace_back(BinarySizes[I]);
    Pointers.push_back(Result[I].data());
  }

  Plugin->call<PiApiKind::piProgramGetInfo>(NativePrg, PI_PROGRAM_INFO_BINARIES,
                                            sizeof(char *) * Pointers.size(),
                                            Pointers.data(), nullptr);

  try {
    OSUtil::makeDir(DirName.c_str());
    LockCacheItem Lock{FileName};
    if (Lock.isOwned()) {
      std::string FullFileName = FileName + ".bin";
      writeBinaryDataToFile(FullFileName, Result);
      trace("device binary has been cached: " + FullFileName);
      writeSourceItem(FileName + ".src", Device, Img, SpecConsts,
                      BuildOptionsString);
    }
  } catch (...) {
    // If a problem happens on storing cache item, do nothing
  }
}

/* Program binaries built for one or more devices are read from persistent
 * cache and returned in form of vector of programs. Each binary program is
 * stored in vector of chars.
 */
std::vector<std::vector<char>> PersistentDeviceCodeCache::getItemFromDisc(
    const device &Device, const RTDeviceBinaryImage &Img,
    const SerializedObj &SpecConsts, const std::string &BuildOptionsString) {

  if (!isImageCached(Img))
    return {};

  std::string Path =
      getCacheItemPath(Device, Img, SpecConsts, BuildOptionsString);

  if (Path.empty() || !OSUtil::isPathPresent(Path))
    return {};

  int i = 0;

  std::string FileName{Path + "/" + std::to_string(i)};
  while (OSUtil::isPathPresent(FileName + ".bin") ||
         OSUtil::isPathPresent(FileName + ".src")) {

    if (!LockCacheItem::isLocked(FileName) &&
        isCacheItemSrcEqual(FileName + ".src", Device, Img, SpecConsts,
                            BuildOptionsString)) {
      try {
        std::string FullFileName = FileName + ".bin";
        std::vector<std::vector<char>> res =
            readBinaryDataFromFile(FullFileName);
        trace("using cached device binary: " + FullFileName);
        return res; // subject for NRVO
      } catch (...) {
        // If read was unsuccessfull try the next item
      }
    }
    FileName = Path + "/" + std::to_string(++i);
  }
  return {};
}

/* Returns string value which can be used to identify different device
 */
std::string PersistentDeviceCodeCache::getDeviceIDString(const device &Device) {
  return Device.get_platform().get_info<sycl::info::platform::name>() + "/" +
         Device.get_info<sycl::info::device::name>() + "/" +
         Device.get_info<sycl::info::device::version>() + "/" +
         Device.get_info<sycl::info::device::driver_version>();
}

/* Write built binary to persistent cache
 * Format: numImages, 1stImageSize, Image[, NthImageSize, NthImage...]
 * Return on first unsuccessfull file operation
 */
void PersistentDeviceCodeCache::writeBinaryDataToFile(
    const std::string &FileName, const std::vector<std::vector<char>> &Data) {
  std::ofstream FileStream{FileName, std::ios::binary};

  size_t Size = Data.size();
  FileStream.write((char *)&Size, sizeof(Size));

  for (size_t i = 0; i < Data.size(); ++i) {
    Size = Data[i].size();
    FileStream.write((char *)&Size, sizeof(Size));
    FileStream.write(Data[i].data(), Size);
  }
  FileStream.close();
  if (FileStream.fail())
    trace("Failed to write binary file " + FileName);
}

/* Read built binary to persistent cache
 * Format: numImages, 1stImageSize, Image[, NthImageSize, NthImage...]
 */
std::vector<std::vector<char>>
PersistentDeviceCodeCache::readBinaryDataFromFile(const std::string &FileName) {
  std::ifstream FileStream{FileName, std::ios::binary};
  size_t ImgNum = 0, ImgSize = 0;
  FileStream.read((char *)&ImgNum, sizeof(ImgNum));

  std::vector<std::vector<char>> Res(ImgNum);
  for (size_t i = 0; i < ImgNum; ++i) {
    FileStream.read((char *)&ImgSize, sizeof(ImgSize));

    std::vector<char> ImgData(ImgSize);
    FileStream.read(ImgData.data(), ImgSize);

    Res[i] = std::move(ImgData);
  }
  FileStream.close();

  if (FileStream.fail()) {
    trace("Failed to read binary file from " + FileName);
    return {};
  }

  return Res;
}

/* Writing cache item key sources to be used for reliable identification
 * Format: Four pairs of [size, value] for device, build options, specialization
 * constant values, device code SPIR-V image.
 */
void PersistentDeviceCodeCache::writeSourceItem(
    const std::string &FileName, const device &Device,
    const RTDeviceBinaryImage &Img, const SerializedObj &SpecConsts,
    const std::string &BuildOptionsString) {
  std::ofstream FileStream{FileName, std::ios::binary};

  std::string DeviceString{getDeviceIDString(Device)};
  size_t Size = DeviceString.size();
  FileStream.write((char *)&Size, sizeof(Size));
  FileStream.write(DeviceString.data(), Size);

  Size = BuildOptionsString.size();
  FileStream.write((char *)&Size, sizeof(Size));
  FileStream.write(BuildOptionsString.data(), Size);

  Size = SpecConsts.size();
  FileStream.write((char *)&Size, sizeof(Size));
  FileStream.write((const char *)SpecConsts.data(), Size);

  Size = Img.getSize();
  FileStream.write((char *)&Size, sizeof(Size));
  FileStream.write((const char *)Img.getRawData().BinaryStart, Size);
  FileStream.close();

  if (FileStream.fail()) {
    trace("Failed to write source file to " + FileName);
  }
}

/* Check that cache item key sources are equal to the current program.
 * If file read operations fail cache item is treated as not equal.
 */
bool PersistentDeviceCodeCache::isCacheItemSrcEqual(
    const std::string &FileName, const device &Device,
    const RTDeviceBinaryImage &Img, const SerializedObj &SpecConsts,
    const std::string &BuildOptionsString) {
  std::ifstream FileStream{FileName, std::ios::binary};

  std::string ImgString{(const char *)Img.getRawData().BinaryStart,
                        Img.getSize()};
  std::string SpecConstsString{(const char *)SpecConsts.data(),
                               SpecConsts.size()};

  size_t Size = 0;
  FileStream.read((char *)&Size, sizeof(Size));
  std::string res(Size, '\0');
  FileStream.read(&res[0], Size);
  if (getDeviceIDString(Device).compare(res))
    return false;

  FileStream.read((char *)&Size, sizeof(Size));
  res.resize(Size);
  FileStream.read(&res[0], Size);
  if (BuildOptionsString.compare(res))
    return false;

  FileStream.read((char *)&Size, sizeof(Size));
  res.resize(Size);
  FileStream.read(&res[0], Size);
  if (SpecConstsString.compare(res))
    return false;

  FileStream.read((char *)&Size, sizeof(Size));
  res.resize(Size);
  FileStream.read(&res[0], Size);
  if (ImgString.compare(res))
    return false;

  FileStream.close();

  if (FileStream.fail()) {
    trace("Failed to read source file from " + FileName);
  }

  return true;
}

/* Returns directory name to store specific kernel image for specified
 * device, build options and specialization constants values.
 */
std::string PersistentDeviceCodeCache::getCacheItemPath(
    const device &Device, const RTDeviceBinaryImage &Img,
    const SerializedObj &SpecConsts, const std::string &BuildOptionsString) {
  std::string cache_root{getRootDir()};
  if (cache_root.empty()) {
    trace("Disable persistent cache due to unconfigured cache root.");
    return {};
  }

  std::string ImgString = "";
  if (Img.getRawData().BinaryStart)
    ImgString.assign((const char *)Img.getRawData().BinaryStart, Img.getSize());

  std::string DeviceString{getDeviceIDString(Device)};
  std::string SpecConstsString{(const char *)SpecConsts.data(),
                               SpecConsts.size()};
  std::hash<std::string> StringHasher{};

  return cache_root + "/" + std::to_string(StringHasher(DeviceString)) + "/" +
         std::to_string(StringHasher(ImgString)) + "/" +
         std::to_string(StringHasher(SpecConstsString)) + "/" +
         std::to_string(StringHasher(BuildOptionsString));
}

/* Returns true if persistent cache is enabled.
 */
bool PersistentDeviceCodeCache::isEnabled() {
  bool CacheIsEnabled = SYCLConfig<SYCL_CACHE_PERSISTENT>::get();
  static bool FirstCheck = true;
  if (FirstCheck) {
    PersistentDeviceCodeCache::trace(CacheIsEnabled ? "enabled" : "disabled");
    FirstCheck = false;
  }
  return CacheIsEnabled;
}

/* Returns path for device code cache root directory
 */
std::string PersistentDeviceCodeCache::getRootDir() {
  return SYCLConfig<SYCL_CACHE_DIR>::get();
}

} // namespace detail
} // namespace _V1
} // namespace sycl

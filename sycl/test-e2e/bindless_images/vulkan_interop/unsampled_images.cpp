// REQUIRES: linux
// REQUIRES: cuda
// REQUIRES: vulkan

// RUN: %clangxx -fsycl -fsycl-targets=%{sycl_triple} %link-vulkan %s -o %t.out
// RUN: %t.out

// Uncomment to print additional test information
// #define VERBOSE_PRINT

#include <sycl/sycl.hpp>

#include "vulkan_common.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

// Helpers and utilities
namespace util {

VkFormat to_vulkan_format(sycl::image_channel_order order,
                          sycl::image_channel_type channelType) {
  if (channelType == sycl::image_channel_type::unsigned_int32) {
    switch (order) {
    case sycl::image_channel_order::r:
      return VK_FORMAT_R32_UINT;
    case sycl::image_channel_order::rg:
      return VK_FORMAT_R32G32_UINT;
    case sycl::image_channel_order::rgba:
      return VK_FORMAT_R32G32B32A32_UINT;
    default: {
      std::cerr << "error in converting to vulkan format\n";
      exit(-1);
    }
    }
  } else if (channelType == sycl::image_channel_type::signed_int32) {
    switch (order) {
    case sycl::image_channel_order::r:
      return VK_FORMAT_R32_SINT;
    case sycl::image_channel_order::rg:
      return VK_FORMAT_R32G32_SINT;
    case sycl::image_channel_order::rgba:
      return VK_FORMAT_R32G32B32A32_SINT;
    default: {
      std::cerr << "error in converting to vulkan format\n";
      exit(-1);
    }
    }
  } else if (channelType == sycl::image_channel_type::fp32) {
    switch (order) {
    case sycl::image_channel_order::r:
      return VK_FORMAT_R32_SFLOAT;
    case sycl::image_channel_order::rg:
      return VK_FORMAT_R32G32_SFLOAT;
    case sycl::image_channel_order::rgba:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    default: {
      std::cerr << "error in converting to vulkan format\n";
      exit(-1);
    }
    }
  } else {
    std::cerr
        << "error in converting to vulkan format - channel type not included\n";
    exit(-1);
  }
}

template <typename DType>
bool is_equal(DType lhs, DType rhs, float epsilon = 0.0001f) {
  if constexpr (std::is_floating_point_v<DType>) {
    return (std::abs(lhs - rhs) < epsilon);
  } else {
    return lhs == rhs;
  }
}

struct handles_t {
  sycl::ext::oneapi::experimental::interop_mem_handle
      input_interop_mem_handle_1,
      input_interop_mem_handle_2, output_interop_mem_handle;
  sycl::ext::oneapi::experimental::interop_semaphore_handle
      sycl_wait_interop_semaphore_handle,
      sycl_done_interop_semaphore_handle;
  sycl::ext::oneapi::experimental::unsampled_image_handle input_1, input_2,
      output;
};

handles_t
create_test_handles(sycl::context &ctxt, sycl::device &dev,
                    int input_image_fd_1, int input_image_fd_2,
                    int output_image_fd, int sycl_wait_semaphore_fd,
                    int sycl_done_semaphore_fd, const size_t img_size,
                    sycl::ext::oneapi::experimental::image_descriptor &desc) {
  namespace syclexp = sycl::ext::oneapi::experimental;
  // Extension: map the external memory descriptors
  syclexp::external_mem_descriptor<syclexp::external_mem_fd>
      input_ext_mem_desc_1{input_image_fd_1, img_size};
  syclexp::external_mem_descriptor<syclexp::external_mem_fd>
      input_ext_mem_desc_2{input_image_fd_2, img_size};
  syclexp::external_mem_descriptor<syclexp::external_mem_fd>
      output_ext_mem_desc{output_image_fd, img_size};

  // Extension: create interop memory handles
  syclexp::interop_mem_handle input_interop_mem_handle_1 =
      syclexp::import_external_memory(input_ext_mem_desc_1, dev, ctxt);
  syclexp::interop_mem_handle input_interop_mem_handle_2 =
      syclexp::import_external_memory(input_ext_mem_desc_2, dev, ctxt);
  syclexp::interop_mem_handle output_interop_mem_handle =
      syclexp::import_external_memory(output_ext_mem_desc, dev, ctxt);

  // Extension: map image memory handles
  syclexp::image_mem_handle input_mapped_mem_handle_1 =
      syclexp::map_external_memory_array(input_interop_mem_handle_1, desc, dev,
                                         ctxt);
  syclexp::image_mem_handle input_mapped_mem_handle_2 =
      syclexp::map_external_memory_array(input_interop_mem_handle_2, desc, dev,
                                         ctxt);
  syclexp::image_mem_handle output_mapped_mem_handle =
      syclexp::map_external_memory_array(output_interop_mem_handle, desc, dev,
                                         ctxt);

  // Extension: create the image and return the handle
  syclexp::unsampled_image_handle input_1 =
      syclexp::create_image(input_mapped_mem_handle_1, desc, dev, ctxt);
  syclexp::unsampled_image_handle input_2 =
      syclexp::create_image(input_mapped_mem_handle_2, desc, dev, ctxt);
  syclexp::unsampled_image_handle output =
      syclexp::create_image(output_mapped_mem_handle, desc, dev, ctxt);

  // Extension: import semaphores
  syclexp::external_semaphore_descriptor<syclexp::external_semaphore_fd>
      sycl_wait_external_semaphore_desc{sycl_wait_semaphore_fd};
  syclexp::external_semaphore_descriptor<syclexp::external_semaphore_fd>
      sycl_done_external_semaphore_desc{sycl_done_semaphore_fd};
  syclexp::interop_semaphore_handle sycl_wait_interop_semaphore_handle =
      syclexp::import_external_semaphore(sycl_wait_external_semaphore_desc, dev,
                                         ctxt);
  syclexp::interop_semaphore_handle sycl_done_interop_semaphore_handle =
      syclexp::import_external_semaphore(sycl_done_external_semaphore_desc, dev,
                                         ctxt);

  return {input_interop_mem_handle_1,
          input_interop_mem_handle_2,
          output_interop_mem_handle,
          sycl_wait_interop_semaphore_handle,
          sycl_done_interop_semaphore_handle,
          input_1,
          input_2,
          output};
}

void cleanup_test(sycl::context &ctxt, sycl::device &dev, handles_t handles) {
  namespace syclexp = sycl::ext::oneapi::experimental;
  syclexp::release_external_memory(handles.input_interop_mem_handle_1, dev,
                                   ctxt);
  syclexp::release_external_memory(handles.input_interop_mem_handle_2, dev,
                                   ctxt);
  syclexp::release_external_memory(handles.output_interop_mem_handle, dev,
                                   ctxt);
  syclexp::destroy_external_semaphore(
      handles.sycl_wait_interop_semaphore_handle, dev, ctxt);
  syclexp::destroy_external_semaphore(
      handles.sycl_done_interop_semaphore_handle, dev, ctxt);
  syclexp::destroy_image_handle(handles.input_1, dev, ctxt);
  syclexp::destroy_image_handle(handles.input_2, dev, ctxt);
  syclexp::destroy_image_handle(handles.output, dev, ctxt);
}

template <typename DType, int NChannels>
void fill_rand(std::vector<sycl::vec<DType, NChannels>> &v) {
  std::default_random_engine generator;
  using distribution_t =
      std::conditional_t<std::is_integral_v<DType>,
                         std::uniform_int_distribution<DType>,
                         std::uniform_real_distribution<DType>>;
  distribution_t distribution(static_cast<DType>(0), static_cast<DType>(100));

  assert(v.empty());
  for (int i = 0; i < v.capacity(); ++i) {
    v.emplace_back(distribution(generator));
  }
}

template <typename T, int NChannels> T add_kernel(T &in_0, T &in_1) {
  if constexpr (std::is_scalar_v<T>) {
    return in_0 + in_1;
  } else {
    T out;
    for (int i = 0; i < NChannels; ++i) {
      out[i] = in_0[i] + in_1[i];
    }
    return out;
  }
}

template <int NDims, typename DType, sycl::image_channel_type CType,
          int NChannels, typename KernelName>
void run_ndim_test(sycl::range<NDims> global_size,
                   sycl::range<NDims> local_size, int input_image_fd_1,
                   int input_image_fd_2, int output_image_fd,
                   int sycl_wait_semaphore_fd, int sycl_done_semaphore_fd) {
  using VecType = sycl::vec<DType, NChannels>;

  sycl::image_channel_order order = sycl::image_channel_order::r;
  if constexpr (NChannels == 2) {
    order = sycl::image_channel_order::rg;
  } else if constexpr (NChannels == 4) {
    order = sycl::image_channel_order::rgba;
  }

  sycl::device dev;
  sycl::queue q(dev);
  auto ctxt = q.get_context();

  namespace syclexp = sycl::ext::oneapi::experimental;

  // Image descriptor - mapped to Vulkan image layout
  syclexp::image_descriptor desc(global_size, order, CType,
                                 syclexp::image_type::interop,
                                 1 /*num_levels*/);

  const size_t img_size = global_size.size() * sizeof(DType) * NChannels;

  auto handles = create_test_handles(
      ctxt, dev, input_image_fd_1, input_image_fd_2, output_image_fd,
      sycl_wait_semaphore_fd, sycl_done_semaphore_fd, img_size, desc);

  // Extension: wait for imported semaphore
  q.ext_oneapi_wait_external_semaphore(
      handles.sycl_wait_interop_semaphore_handle);

  try {
    q.submit([&](sycl::handler &cgh) {
      cgh.parallel_for<KernelName>(
          sycl::nd_range<NDims>{global_size, local_size},
          [=](sycl::nd_item<NDims> it) {
            size_t dim0 = it.get_global_id(0);
            size_t dim1 = it.get_global_id(1);

            if constexpr (NDims == 2) {
              if constexpr (NChannels > 1) {
                VecType px1 = syclexp::read_image<VecType>(
                    handles.input_1, sycl::int2(dim0, dim1));
                VecType px2 = syclexp::read_image<VecType>(
                    handles.input_2, sycl::int2(dim0, dim1));

                auto sum =
                    VecType(util::add_kernel<VecType, NChannels>(px1, px2));
                syclexp::write_image<VecType>(
                    handles.output, sycl::int2(dim0, dim1), VecType(sum));
              } else {
                DType px1 = syclexp::read_image<DType>(handles.input_1,
                                                       sycl::int2(dim0, dim1));
                DType px2 = syclexp::read_image<DType>(handles.input_2,
                                                       sycl::int2(dim0, dim1));

                auto sum = DType(util::add_kernel<DType, NChannels>(px1, px2));
                syclexp::write_image<DType>(handles.output,
                                            sycl::int2(dim0, dim1), DType(sum));
              }
            } else {
              size_t dim2 = it.get_global_id(2);

              if constexpr (NChannels > 1) {
                VecType px1 = syclexp::read_image<VecType>(
                    handles.input_1, sycl::int4(dim0, dim1, dim2, 0));
                VecType px2 = syclexp::read_image<VecType>(
                    handles.input_2, sycl::int4(dim0, dim1, dim2, 0));

                auto sum =
                    VecType(util::add_kernel<VecType, NChannels>(px1, px2));
                syclexp::write_image<VecType>(handles.output,
                                              sycl::int4(dim0, dim1, dim2, 0),
                                              VecType(sum));
              } else {
                DType px1 = syclexp::read_image<DType>(
                    handles.input_1, sycl::int4(dim0, dim1, dim2, 0));
                DType px2 = syclexp::read_image<DType>(
                    handles.input_2, sycl::int4(dim0, dim1, dim2, 0));

                auto sum = DType(util::add_kernel<DType, NChannels>(px1, px2));
                syclexp::write_image<DType>(handles.output,
                                            sycl::int4(dim0, dim1, dim2, 0),
                                            DType(sum));
              }
            }
          });
    });

    // Extension: signal imported semaphore
    q.submit([&](sycl::handler &cgh) {
      cgh.ext_oneapi_signal_external_semaphore(
          handles.sycl_done_interop_semaphore_handle);
    });

    // Wait for kernel completion before destroying external objects
    q.wait_and_throw();
  } catch (sycl::exception e) {
    std::cerr << "\tKernel submission failed! " << e.what() << std::endl;
    exit(-1);
  } catch (...) {
    std::cerr << "\tKernel submission failed!" << std::endl;
    exit(-1);
  }
}
} // namespace util

auto createImageMemoryBarrier(VkImage &img) {
  VkImageMemoryBarrier barrierInput = {};
  barrierInput.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrierInput.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrierInput.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrierInput.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrierInput.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrierInput.image = img;
  barrierInput.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrierInput.subresourceRange.levelCount = 1;
  barrierInput.subresourceRange.layerCount = 1;
  barrierInput.srcAccessMask = 0;
  barrierInput.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  return barrierInput;
}

struct vulkan_image_test_resources_t {
  VkImage vkImage;
  VkDeviceMemory imageMemory;
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingMemory;

  vulkan_image_test_resources_t(VkImageType imgType, VkFormat format,
                                VkExtent3D ext, const size_t imageSizeBytes) {
    vkImage = vkutil::createImage(imgType, format, ext,
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                      VK_IMAGE_USAGE_STORAGE_BIT);
    auto inputImageMemoryTypeIndex = vkutil::getImageMemoryTypeIndex(
        vkImage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    imageMemory =
        vkutil::allocateDeviceMemory(imageSizeBytes, inputImageMemoryTypeIndex);
    VK_CHECK_CALL(
        vkBindImageMemory(vk_device, vkImage, imageMemory, 0 /*memoryOffset*/));

    stagingBuffer = vkutil::createBuffer(imageSizeBytes,
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto inputStagingMemoryTypeIndex = vkutil::getBufferMemoryTypeIndex(
        stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingMemory = vkutil::allocateDeviceMemory(
        imageSizeBytes, inputStagingMemoryTypeIndex, false /*exportable*/);
    VK_CHECK_CALL(vkBindBufferMemory(vk_device, stagingBuffer, stagingMemory,
                                     0 /*memoryOffset*/));
  }

  ~vulkan_image_test_resources_t() {
    vkDestroyBuffer(vk_device, stagingBuffer, nullptr);
    vkDestroyImage(vk_device, vkImage, nullptr);
    vkFreeMemory(vk_device, stagingMemory, nullptr);
    vkFreeMemory(vk_device, imageMemory, nullptr);
  }
};

template <int NDims, typename DType, int NChannels,
          sycl::image_channel_type CType, sycl::image_channel_order COrder,
          typename KernelName>
bool run_test(sycl::range<NDims> dims, sycl::range<NDims> local_size,
              unsigned int seed = 0) {
  uint32_t width = static_cast<uint32_t>(dims[0]);
  uint32_t height = 1;
  uint32_t depth = 1;

  size_t num_elems = dims[0];
  VkImageType imgType = VK_IMAGE_TYPE_1D;

  if (NDims > 1) {
    num_elems *= dims[1];
    height = static_cast<uint32_t>(dims[1]);
    imgType = VK_IMAGE_TYPE_2D;
  }
  if (NDims > 2) {
    num_elems *= dims[2];
    depth = static_cast<uint32_t>(dims[2]);
    imgType = VK_IMAGE_TYPE_3D;
  }

  VkFormat format = util::to_vulkan_format(COrder, CType);
  const size_t imageSizeBytes = num_elems * NChannels * sizeof(DType);

  vulkan_image_test_resources_t inVkImgRes1(
      imgType, format, {width, height, depth}, imageSizeBytes);
  vulkan_image_test_resources_t inVkImgRes2(
      imgType, format, {width, height, depth}, imageSizeBytes);
  vulkan_image_test_resources_t outVkImgRes(
      imgType, format, {width, height, depth}, imageSizeBytes);

  printString("Populating staging buffer\n");
  // Populate staging memory
  using VecType = sycl::vec<DType, NChannels>;
  std::vector<VecType> input_vector_0;
  input_vector_0.reserve(num_elems);
  std::srand(seed);
  util::fill_rand(input_vector_0);

  VecType *inputStagingData = nullptr;
  VK_CHECK_CALL(vkMapMemory(vk_device, inVkImgRes1.stagingMemory, 0 /*offset*/,
                            imageSizeBytes, 0 /*flags*/,
                            (void **)&inputStagingData));
  for (int i = 0; i < num_elems; ++i) {
    inputStagingData[i] = input_vector_0[i];
  }
  vkUnmapMemory(vk_device, inVkImgRes1.stagingMemory);

  std::vector<VecType> input_vector_1;
  input_vector_1.reserve(num_elems);
  std::srand(seed);
  util::fill_rand(input_vector_1);

  VK_CHECK_CALL(vkMapMemory(vk_device, inVkImgRes2.stagingMemory, 0 /*offset*/,
                            imageSizeBytes, 0 /*flags*/,
                            (void **)&inputStagingData));
  for (int i = 0; i < num_elems; ++i) {
    inputStagingData[i] = input_vector_1[i];
  }
  vkUnmapMemory(vk_device, inVkImgRes2.stagingMemory);

  printString("Submitting image layout transition\n");
  // Transition image layouts
  {
    VkImageMemoryBarrier barrierInput1 =
        createImageMemoryBarrier(inVkImgRes1.vkImage);
    VkImageMemoryBarrier barrierInput2 =
        createImageMemoryBarrier(inVkImgRes2.vkImage);

    VkImageMemoryBarrier barrierOutput =
        createImageMemoryBarrier(outVkImgRes.vkImage);

    VkCommandBufferBeginInfo cbbi = {};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK_CALL(vkBeginCommandBuffer(vk_computeCmdBuffer, &cbbi));
    vkCmdPipelineBarrier(vk_computeCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrierInput1);

    vkCmdPipelineBarrier(vk_computeCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrierInput2);

    vkCmdPipelineBarrier(vk_computeCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrierOutput);
    VK_CHECK_CALL(vkEndCommandBuffer(vk_computeCmdBuffer));

    VkSubmitInfo submission = {};
    submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submission.commandBufferCount = 1;
    submission.pCommandBuffers = &vk_computeCmdBuffer;

    VK_CHECK_CALL(vkQueueSubmit(vk_compute_queue, 1 /*submitCount*/,
                                &submission, VK_NULL_HANDLE /*fence*/));
    VK_CHECK_CALL(vkQueueWaitIdle(vk_compute_queue));
  }

  // Create semaphore to later import in SYCL
  printString("Creating semaphores\n");
  VkSemaphore syclWaitSemaphore;
  {
    VkExportSemaphoreCreateInfo esci = {};
    esci.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    esci.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sci.pNext = &esci;
    VK_CHECK_CALL(
        vkCreateSemaphore(vk_device, &sci, nullptr, &syclWaitSemaphore));
  }

  VkSemaphore syclDoneSemaphore;
  {
    VkExportSemaphoreCreateInfo esci = {};
    esci.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    esci.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sci.pNext = &esci;
    VK_CHECK_CALL(
        vkCreateSemaphore(vk_device, &sci, nullptr, &syclDoneSemaphore));
  }

  printString("Copying staging memory to images\n");
  // Copy staging to main image memory
  {
    VkCommandBufferBeginInfo cbbi = {};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    VkBufferImageCopy copyRegion = {};
    copyRegion.imageExtent = {width, height, depth};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;

    VK_CHECK_CALL(vkBeginCommandBuffer(vk_transferCmdBuffers[0], &cbbi));
    vkCmdCopyBufferToImage(vk_transferCmdBuffers[0], inVkImgRes1.stagingBuffer,
                           inVkImgRes1.vkImage, VK_IMAGE_LAYOUT_GENERAL,
                           1 /*regionCount*/, &copyRegion);
    vkCmdCopyBufferToImage(vk_transferCmdBuffers[0], inVkImgRes2.stagingBuffer,
                           inVkImgRes2.vkImage, VK_IMAGE_LAYOUT_GENERAL,
                           1 /*regionCount*/, &copyRegion);
    VK_CHECK_CALL(vkEndCommandBuffer(vk_transferCmdBuffers[0]));

    std::vector<VkPipelineStageFlags> stages{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};

    VkSubmitInfo submission = {};
    submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submission.commandBufferCount = 1;
    submission.pCommandBuffers = &vk_transferCmdBuffers[0];

    submission.signalSemaphoreCount = 1;
    submission.pSignalSemaphores = &syclWaitSemaphore;
    submission.pWaitDstStageMask = stages.data();

    VK_CHECK_CALL(vkQueueSubmit(vk_transfer_queue, 1 /*submitCount*/,
                                &submission, VK_NULL_HANDLE /*fence*/));
  }

  printString("Getting memory file descriptors and calling into SYCL\n");
  // Pass memory to SYCL for modification

  auto global_size = dims;
  auto input_fd_1 = vkutil::getMemoryOpaqueFD(inVkImgRes1.imageMemory);
  auto input_fd_2 = vkutil::getMemoryOpaqueFD(inVkImgRes2.imageMemory);
  auto output_fd = vkutil::getMemoryOpaqueFD(outVkImgRes.imageMemory);

  // Pass semaphores to SYCL for synchronization
  int sycl_wait_semaphore_fd = vkutil::getSemaphoreOpaqueFD(syclWaitSemaphore);
  int sycl_done_semaphore_fd = vkutil::getSemaphoreOpaqueFD(syclDoneSemaphore);

  util::run_ndim_test<NDims, DType, CType, NChannels, KernelName>(
      global_size, local_size, input_fd_1, input_fd_2, output_fd,
      sycl_wait_semaphore_fd, sycl_done_semaphore_fd);

  printString("Copying image memory to staging memory\n");
  // Copy main image memory to staging
  {
    VkCommandBufferBeginInfo cbbi = {};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    VkBufferImageCopy copyRegion = {};
    copyRegion.imageExtent = {width, height, depth};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;

    VK_CHECK_CALL(vkBeginCommandBuffer(vk_transferCmdBuffers[1], &cbbi));
    vkCmdCopyImageToBuffer(vk_transferCmdBuffers[1], outVkImgRes.vkImage,
                           VK_IMAGE_LAYOUT_GENERAL, outVkImgRes.stagingBuffer,
                           1 /*regionCount*/, &copyRegion);
    VK_CHECK_CALL(vkEndCommandBuffer(vk_transferCmdBuffers[1]));

    std::vector<VkPipelineStageFlags> stages{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};

    VkSubmitInfo submission = {};
    submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submission.commandBufferCount = 1;
    submission.pCommandBuffers = &vk_transferCmdBuffers[1];

    submission.waitSemaphoreCount = 1;
    submission.pWaitSemaphores = &syclDoneSemaphore;
    submission.pWaitDstStageMask = stages.data();

    VK_CHECK_CALL(vkQueueSubmit(vk_transfer_queue, 1 /*submitCount*/,
                                &submission, VK_NULL_HANDLE /*fence*/));
    VK_CHECK_CALL(vkQueueWaitIdle(vk_transfer_queue));
  }

  printString("Validating\n");
  // Validate that SYCL made changes to the memory
  bool validated = true;
  VecType *outputStagingData = nullptr;
  VK_CHECK_CALL(vkMapMemory(vk_device, outVkImgRes.stagingMemory, 0 /*offset*/,
                            imageSizeBytes, 0 /*flags*/,
                            (void **)&outputStagingData));
  for (int i = 0; i < num_elems; ++i) {
    VecType expected = input_vector_0[i] + input_vector_1[i];
    for (int j = 0; j < NChannels; ++j) {
      // Use helper function to determine if data is accepted
      // For integers, exact results are expected
      // For floats, accepted error variance is passed
      if (!util::is_equal(outputStagingData[i][j], expected[j])) {
        std::cerr << "Result mismatch! actual[" << i << "][" << j
                  << "] == " << outputStagingData[i][j]
                  << " : expected == " << expected[j] << "\n";
        validated = false;
      }
    }
    if (!validated)
      break;
  }
  vkUnmapMemory(vk_device, outVkImgRes.stagingMemory);

  if (validated) {
    printString("  Results are correct!\n");
  }

  // Cleanup
  vkDestroySemaphore(vk_device, syclWaitSemaphore, nullptr);
  vkDestroySemaphore(vk_device, syclDoneSemaphore, nullptr);

  return validated;
}

bool run_all() {
  unsigned int seed = 0;

  printString("Running 3D uint4\n");
  bool valid = run_test<3, uint32_t, 4, sycl::image_channel_type::signed_int32,
                        sycl::image_channel_order::rgba, class uint4_3d>(
      {272, 144, 4}, {16, 16, 4}, seed);

  printString("Running 3D uint2\n");
  valid &= run_test<3, uint32_t, 2, sycl::image_channel_type::unsigned_int32,
                    sycl::image_channel_order::rg, class uint2_3d>(
      {272, 144, 4}, {16, 16, 4}, seed);

  printString("Running 3D uint\n");
  valid &= run_test<3, uint32_t, 1, sycl::image_channel_type::unsigned_int32,
                    sycl::image_channel_order::r, class uint1_3d>(
      {272, 144, 4}, {16, 16, 4}, seed);

  printString("Running 3D float4\n");
  valid &= run_test<3, float, 4, sycl::image_channel_type::fp32,
                    sycl::image_channel_order::rgba, class float4_3d>(
      {16, 16, 16}, {16, 16, 4}, seed);

  printString("Running 3D float2\n");
  valid &= run_test<3, float, 2, sycl::image_channel_type::fp32,
                    sycl::image_channel_order::rg, class float2_3d>(
      {128, 128, 16}, {16, 16, 4}, seed);
  printString("Running 3D float\n");
  valid &= run_test<3, float, 1, sycl::image_channel_type::fp32,
                    sycl::image_channel_order::r, class float1_3d>(
      {1024, 1024, 16}, {16, 16, 4}, seed);

  printString("Running 2D uint4\n");
  valid &= run_test<2, uint32_t, 4, sycl::image_channel_type::unsigned_int32,
                    sycl::image_channel_order::rgba, class uint4_2d>(
      {1024, 1024}, {2, 2}, seed);

  printString("Running 2D uint2\n");
  valid &= run_test<2, uint32_t, 2, sycl::image_channel_type::unsigned_int32,
                    sycl::image_channel_order::rg, class uint2_2d>(
      {1024, 1024}, {2, 2}, seed);

  printString("Running 2D uint\n");
  valid &= run_test<2, uint32_t, 1, sycl::image_channel_type::unsigned_int32,
                    sycl::image_channel_order::r, class uint1_2d>({512, 512},
                                                                  {2, 2}, seed);

  printString("Running 2D float4\n");
  valid &= run_test<2, float, 4, sycl::image_channel_type::fp32,
                    sycl::image_channel_order::rgba, class float4_2d>(
      {128, 64}, {2, 2}, seed);

  printString("Running 2D float2\n");
  valid &= run_test<2, float, 2, sycl::image_channel_type::fp32,
                    sycl::image_channel_order::rg, class float2_2d>(
      {1024, 512}, {2, 2}, seed);

  printString("Running 2D float\n");
  valid &= run_test<2, float, 1, sycl::image_channel_type::fp32,
                    sycl::image_channel_order::r, class float1_2d>(
      {32, 32}, {2, 2}, seed);

  if (!valid) {
    std::cout << "Unsampled images test has failure(s)\n";
  } else {
    std::cout << "Unsampled images test passes\n";
  }
  return valid;
}

int main() {

  if (vkutil::setupInstance() != VK_SUCCESS) {
    std::cerr << "Instance setup failed!\n";
    return EXIT_FAILURE;
  }

  // Currently only Nvidia devices are tested
  if (vkutil::setupDevice("NVIDIA") != VK_SUCCESS) {
    std::cerr << "Device setup failed!\n";
    return EXIT_FAILURE;
  }

  if (vkutil::setupCommandBuffers() != VK_SUCCESS) {
    std::cerr << "Command buffers setup failed!\n";
    return EXIT_FAILURE;
  }

  auto run_ok = run_all();

  if (vkutil::cleanup() != VK_SUCCESS) {
    std::cerr << "Cleanup failed!\n";
    return EXIT_FAILURE;
  }

  return run_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

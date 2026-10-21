#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#define CHECK(x, msg) do { VkResult r = (x); if (r != VK_SUCCESS) { printf("FAILED %s: VkResult=%d\n", msg, r); return 1; } printf("%s ok\n", msg); } while (0)

static PFN_vkGetInstanceProcAddr icd_gpa;
static VkInstance instance;
static PFN_vkGetDeviceProcAddr GetDeviceProcAddr;

#define IPROC(name) (PFN_##name)icd_gpa(instance, #name)
#define DPROC(dev, name) (PFN_##name)GetDeviceProcAddr(dev, #name)

#define IMG_W 4
#define IMG_H 4

static int load_file(const char *path, uint8_t *buf, size_t buf_cap, size_t *size_out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, buf_cap);
    close(fd);
    if (n < 0) return -1;
    *size_out = (size_t)n;
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *libpath = getenv("PANVK_TEST_LIB"); if (!libpath || !libpath[0]) libpath = "/data/data/com.termux/files/home/panvk-luke/build-termux/src/panfrost/vulkan/libvulkan_panfrost.so"; void *lib = dlopen(libpath, RTLD_NOW);
    if (!lib) { printf("dlopen failed\n"); return 1; }
    icd_gpa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");

    PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)icd_gpa(NULL, "vkCreateInstance");
    VkApplicationInfo app_info = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo inst_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app_info };
    CHECK(CreateInstance(&inst_info, NULL, &instance), "vkCreateInstance");

    GetDeviceProcAddr = IPROC(vkGetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = IPROC(vkEnumeratePhysicalDevices);
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemoryProperties = IPROC(vkGetPhysicalDeviceMemoryProperties);
    PFN_vkCreateDevice CreateDevice = IPROC(vkCreateDevice);

    uint32_t count = 1;
    VkPhysicalDevice pdev;
    EnumeratePhysicalDevices(instance, &count, &pdev);

    float priority = 1.0f;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties =
        IPROC(vkEnumerateDeviceExtensionProperties);
    PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 =
        IPROC(vkGetPhysicalDeviceFeatures2);
    if (!EnumerateDeviceExtensionProperties || !GetPhysicalDeviceFeatures2) {
        printf("missing device extension/features2 query (ext=%p features2=%p)\n",
               (void *)EnumerateDeviceExtensionProperties,
               (void *)GetPhysicalDeviceFeatures2);
        return 1;
    }

    VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT roaa_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &roaa_features,
    };
    GetPhysicalDeviceFeatures2(pdev, &features2);

    uint32_t ext_count = 0;
    EnumerateDeviceExtensionProperties(pdev, NULL, &ext_count, NULL);
    VkExtensionProperties *exts = calloc(ext_count, sizeof(*exts));
    EnumerateDeviceExtensionProperties(pdev, NULL, &ext_count, exts);
    bool has_roaa = false;
    for (uint32_t i = 0; i < ext_count; i++) {
        if (!strcmp(exts[i].extensionName,
                    VK_EXT_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME))
            has_roaa = true;
    }
    printf("ROAA extension=%s color=%u depth=%u stencil=%u\n",
           has_roaa ? "yes" : "NO",
           roaa_features.rasterizationOrderColorAttachmentAccess,
           roaa_features.rasterizationOrderDepthAttachmentAccess,
           roaa_features.rasterizationOrderStencilAttachmentAccess);
    free(exts);

    const char *enabled_exts[] = {
        VK_EXT_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME,
    };
    VkDeviceQueueCreateInfo qinfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo dinfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qinfo,
        .enabledExtensionCount = has_roaa ? 1 : 0,
        .ppEnabledExtensionNames = enabled_exts,
        .pNext = has_roaa ? &roaa_features : NULL };
    VkDevice device;
    CHECK(CreateDevice(pdev, &dinfo, NULL, &device), "vkCreateDevice");

    PFN_vkCreateImage CreateImage = DPROC(device, vkCreateImage);
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = DPROC(device, vkGetImageMemoryRequirements);
    PFN_vkAllocateMemory AllocateMemory = DPROC(device, vkAllocateMemory);
    PFN_vkBindImageMemory BindImageMemory = DPROC(device, vkBindImageMemory);
    PFN_vkCreateImageView CreateImageView = DPROC(device, vkCreateImageView);

    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { IMG_W, IMG_H, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image;
    CHECK(CreateImage(device, &img_info, NULL, &image), "vkCreateImage");

    VkMemoryRequirements img_mem_req;
    GetImageMemoryRequirements(device, image, &img_mem_req);

    VkPhysicalDeviceMemoryProperties mem_props;
    GetMemoryProperties(pdev, &mem_props);
    uint32_t dev_mem_type = 0;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if (img_mem_req.memoryTypeBits & (1 << i)) { dev_mem_type = i; break; }
    }

    VkMemoryAllocateInfo img_alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = img_mem_req.size, .memoryTypeIndex = dev_mem_type };
    VkDeviceMemory img_memory;
    CHECK(AllocateMemory(device, &img_alloc, NULL, &img_memory), "vkAllocateMemory(image)");
    CHECK(BindImageMemory(device, image, img_memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkImageView image_view;
    CHECK(CreateImageView(device, &view_info, NULL, &image_view), "vkCreateImageView");

    PFN_vkCreateRenderPass CreateRenderPass = DPROC(device, vkCreateRenderPass);
    PFN_vkCreateFramebuffer CreateFramebuffer = DPROC(device, vkCreateFramebuffer);

    VkAttachmentDescription attach = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    };
    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_GENERAL };
    VkAttachmentReference input_ref = { 0, VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription subpass = {
        .flags = VK_SUBPASS_DESCRIPTION_RASTERIZATION_ORDER_ATTACHMENT_COLOR_ACCESS_BIT_EXT,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 1,
        .pInputAttachments = &input_ref,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };
    VkSubpassDependency dependency = {
        .srcSubpass = 0,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    };
    VkRenderPassCreateInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attach,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    VkRenderPass render_pass;
    CHECK(CreateRenderPass(device, &rp_info, NULL, &render_pass), "vkCreateRenderPass");

    VkFramebufferCreateInfo fb_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .pAttachments = &image_view,
        .width = IMG_W,
        .height = IMG_H,
        .layers = 1,
    };
    VkFramebuffer framebuffer;
    CHECK(CreateFramebuffer(device, &fb_info, NULL, &framebuffer), "vkCreateFramebuffer");
    printf("renderpass+framebuffer ready\n");

    PFN_vkCreateShaderModule CreateShaderModule = DPROC(device, vkCreateShaderModule);
    PFN_vkCreatePipelineLayout CreatePipelineLayout = DPROC(device, vkCreatePipelineLayout);
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = DPROC(device, vkCreateGraphicsPipelines);

    size_t vert_size, frag_size;
    uint8_t vert_code[8192];
    uint8_t frag_code[8192];
    if (load_file("/data/data/com.termux/files/home/panvk-luke/tests/triangle_vert.spv", vert_code, sizeof(vert_code), &vert_size) < 0) {
        printf("failed to load vert shader\n"); return 1;
    }
    if (load_file("/data/data/com.termux/files/home/panvk-luke/tests/triangle_frag.spv", frag_code, sizeof(frag_code), &frag_size) < 0) {
        printf("failed to load frag shader\n"); return 1;
    }
    printf("shaders loaded: vert=%zu frag=%zu bytes\n", vert_size, frag_size);

    VkShaderModuleCreateInfo vsmi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = vert_size, .pCode = (uint32_t*)vert_code };
    VkShaderModule vert_module;
    CHECK(CreateShaderModule(device, &vsmi, NULL, &vert_module), "vkCreateShaderModule(vert)");

    VkShaderModuleCreateInfo fsmi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = frag_size, .pCode = (uint32_t*)frag_code };
    VkShaderModule frag_module;
    CHECK(CreateShaderModule(device, &fsmi, NULL, &frag_module), "vkCreateShaderModule(frag)");

    size_t roaa_frag_size;
    uint8_t roaa_frag_code[8192];
    if (load_file("/data/data/com.termux/files/home/panvk-luke/tests/roaa_frag.spv", roaa_frag_code, sizeof(roaa_frag_code), &roaa_frag_size) < 0) {
        printf("failed to load roaa frag shader\n"); return 1;
    }
    VkShaderModuleCreateInfo rfsmi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = roaa_frag_size, .pCode = (uint32_t*)roaa_frag_code };
    VkShaderModule roaa_frag_module;
    CHECK(CreateShaderModule(device, &rfsmi, NULL, &roaa_frag_module), "vkCreateShaderModule(roaa frag)");

    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = DPROC(device, vkCreateDescriptorSetLayout);
    PFN_vkCreateDescriptorPool CreateDescriptorPool = DPROC(device, vkCreateDescriptorPool);
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets = DPROC(device, vkAllocateDescriptorSets);
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets = DPROC(device, vkUpdateDescriptorSets);

    VkDescriptorSetLayoutBinding ia_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsl_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &ia_binding,
    };
    VkDescriptorSetLayout dsl;
    CHECK(CreateDescriptorSetLayout(device, &dsl_info, NULL, &dsl), "vkCreateDescriptorSetLayout");

    VkPipelineLayoutCreateInfo pl_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout playout;
    CHECK(CreatePipelineLayout(device, &pl_info, NULL, &playout), "vkCreatePipelineLayout");

    VkDescriptorPoolSize ia_pool_size = { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1 };
    VkDescriptorPoolCreateInfo dp_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &ia_pool_size,
    };
    VkDescriptorPool desc_pool;
    CHECK(CreateDescriptorPool(device, &dp_info, NULL, &desc_pool), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo ds_alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &dsl,
    };
    VkDescriptorSet desc_set;
    CHECK(AllocateDescriptorSets(device, &ds_alloc, &desc_set), "vkAllocateDescriptorSets");

    VkDescriptorImageInfo ia_image = {
        .imageView = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet ia_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = desc_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
        .pImageInfo = &ia_image,
    };
    UpdateDescriptorSets(device, 1, &ia_write, 0, NULL);
    printf("input-attachment descriptor ready\n");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
    };
    VkPipelineShaderStageCreateInfo stages_roaa[2] = { stages[0], stages[1] };
    stages_roaa[1].module = roaa_frag_module;

    VkPipelineVertexInputStateCreateInfo vi_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };

    VkViewport viewport = { 0, 0, IMG_W, IMG_H, 0, 1 };
    VkRect2D scissor = { {0,0}, {IMG_W, IMG_H} };
    VkPipelineViewportStateCreateInfo vp_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };

    VkPipelineRasterizationStateCreateInfo rs_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineColorBlendAttachmentState cb_attach = { .colorWriteMask = 0xF };
    VkPipelineColorBlendStateCreateInfo cb_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cb_attach };

    VkGraphicsPipelineCreateInfo gp_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi_state,
        .pInputAssemblyState = &ia_state,
        .pViewportState = &vp_state,
        .pRasterizationState = &rs_state,
        .pMultisampleState = &ms_state,
        .pColorBlendState = &cb_state,
        .layout = playout,
        .renderPass = render_pass,
        .subpass = 0,
    };
    VkPipeline pipeline;
    CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_info, NULL, &pipeline), "vkCreateGraphicsPipelines");

    VkPipelineColorBlendStateCreateInfo cb_state_roaa = cb_state;
    cb_state_roaa.flags =
        VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT;
    VkGraphicsPipelineCreateInfo gp_info_roaa = gp_info;
    gp_info_roaa.pStages = stages_roaa;
    gp_info_roaa.pColorBlendState = &cb_state_roaa;
    VkPipeline pipeline_roaa;
    CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_info_roaa, NULL, &pipeline_roaa),
          "vkCreateGraphicsPipelines(roaa)");

    PFN_vkCreateBuffer CreateBuffer = DPROC(device, vkCreateBuffer);
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = DPROC(device, vkGetBufferMemoryRequirements);
    PFN_vkBindBufferMemory BindBufferMemory = DPROC(device, vkBindBufferMemory);
    PFN_vkMapMemory MapMemory = DPROC(device, vkMapMemory);

    VkDeviceSize readback_size = IMG_W * IMG_H * 4;
    VkBufferCreateInfo rb_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = readback_size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer readback_buf;
    CHECK(CreateBuffer(device, &rb_info, NULL, &readback_buf), "vkCreateBuffer(readback)");

    VkMemoryRequirements rb_mem_req;
    GetBufferMemoryRequirements(device, readback_buf, &rb_mem_req);
    uint32_t host_mem_type = 0;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((rb_mem_req.memoryTypeBits & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            host_mem_type = i; break;
        }
    }
    VkMemoryAllocateInfo rb_alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rb_mem_req.size, .memoryTypeIndex = host_mem_type };
    VkDeviceMemory rb_memory;
    CHECK(AllocateMemory(device, &rb_alloc, NULL, &rb_memory), "vkAllocateMemory(readback)");
    CHECK(BindBufferMemory(device, readback_buf, rb_memory, 0), "vkBindBufferMemory(readback)");

    void *rb_mapped;
    CHECK(MapMemory(device, rb_memory, 0, readback_size, 0, &rb_mapped), "vkMapMemory(readback)");
    memset(rb_mapped, 0x11, readback_size);
    printf("readback buffer ready, initial byte=0x%02x\n", ((uint8_t*)rb_mapped)[0]);

    PFN_vkCreateCommandPool CreateCommandPool = DPROC(device, vkCreateCommandPool);
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = DPROC(device, vkAllocateCommandBuffers);
    PFN_vkBeginCommandBuffer BeginCommandBuffer = DPROC(device, vkBeginCommandBuffer);
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass = DPROC(device, vkCmdBeginRenderPass);
    PFN_vkCmdBindPipeline CmdBindPipeline = DPROC(device, vkCmdBindPipeline);
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = DPROC(device, vkCmdBindDescriptorSets);
    PFN_vkCmdDraw CmdDraw = DPROC(device, vkCmdDraw);
    PFN_vkCmdEndRenderPass CmdEndRenderPass = DPROC(device, vkCmdEndRenderPass);
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer = DPROC(device, vkCmdCopyImageToBuffer);
    PFN_vkEndCommandBuffer EndCommandBuffer = DPROC(device, vkEndCommandBuffer);
    PFN_vkGetDeviceQueue GetDeviceQueue = DPROC(device, vkGetDeviceQueue);
    PFN_vkQueueSubmit QueueSubmit = DPROC(device, vkQueueSubmit);
    PFN_vkQueueWaitIdle QueueWaitIdle = DPROC(device, vkQueueWaitIdle);

    VkCommandPoolCreateInfo cp_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHECK(CreateCommandPool(device, &cp_info, NULL, &cpool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo cb_alloc = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmdbuf;
    CHECK(AllocateCommandBuffers(device, &cb_alloc, &cmdbuf), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(BeginCommandBuffer(cmdbuf, &begin_info), "vkBeginCommandBuffer");

    VkClearValue clear = { .color = { .float32 = {0.0f, 0.0f, 0.0f, 1.0f} } };
    VkRenderPassBeginInfo rp_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = { {0,0}, {IMG_W, IMG_H} },
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    CmdBeginRenderPass(cmdbuf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    CmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    CmdDraw(cmdbuf, 3, 1, 0, 0);
    CmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_roaa);
    CmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0, 1,
                          &desc_set, 0, NULL);
    CmdDraw(cmdbuf, 3, 1, 0, 0);
    CmdEndRenderPass(cmdbuf);

    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { IMG_W, IMG_H, 1 },
    };
    CmdCopyImageToBuffer(cmdbuf, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buf, 1, &region);

    CHECK(EndCommandBuffer(cmdbuf), "vkEndCommandBuffer");
    printf("command buffer recorded (draw + copy)\n");

    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);
    VkSubmitInfo submit_info = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmdbuf };
    printf("calling vkQueueSubmit (draw + fragment path)...\n");
    CHECK(QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit");
    CHECK(QueueWaitIdle(queue), "vkQueueWaitIdle");

    uint8_t *pixels = (uint8_t *)rb_mapped;
    printf("center pixel RGBA = %d,%d,%d,%d (expected 255,0,255,255 = magenta)\n",
           pixels[0], pixels[1], pixels[2], pixels[3]);
    if (pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 255 && pixels[3] == 255)
        printf("ROAA_INPUT_ATTACHMENT_OK\n");
    else
        printf("ROAA_INPUT_ATTACHMENT_MISMATCH\n");

    return 0;
}

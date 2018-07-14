#include "QuarkWindow.h"

#include <iostream>

#include "Scene.h"
#include "Transform.h"
#include "Mesh.h"
#include "QuarkObject.h"
#include "Resource.h"
#include "VulkanTools.h"
#include "VulkanDevice.h"
#include "CameraController.h"
#include "VulkanBuffer.h"
#include "Material.h"
#include "MeshFilter.h"
#include "MeshRenderer.h"
#include "QuarkString.h"
#include "AwakeBehaviour.h"


qe::edit::QuarkWindow::QuarkWindow()
{
    vi_device_ = std::make_shared<qe::render::vulkan::VulkanDevice>(reinterpret_cast<HWND>(this->winId()));
    resource_ = std::make_shared<qe::core::Resource>();
    camera_controller_ = std::make_shared<qe::edit::CameraController>();

    light_dir_ = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
}

qe::edit::QuarkWindow::~QuarkWindow()
{
    

}

void qe::edit::QuarkWindow::Init()
{
    LoadScene(std::dynamic_pointer_cast<qe::core::Scene>(resource_->Load(kModelPath)));

    Awake();

    CreateCommandBuffer();

    graphics_timer_ = std::make_shared<QTimer>();
    graphics_timer_->setInterval(1);
    connect(graphics_timer_.get(), SIGNAL(timeout()), this, SLOT(update()));
    graphics_timer_->start(1);

    fps_timer_ = std::make_shared<QTime>();
    fps_timer_->start();

    is_init_vulkan_ = true;
}

void qe::edit::QuarkWindow::resizeEvent(QResizeEvent * event)
{
    auto size = event->size();
    auto width = size.width();
    auto height = size.height();

    if (height <= 0) return;

    camera_controller_->UpdateProjectionRatio((float)width / height);

    UpdateUniformBuffer();
}

void qe::edit::QuarkWindow::mouseMoveEvent(QMouseEvent * event)
{
    if (!right_button_press_) {
        return;
    }

    if (!init_mouse_pos_) {
        init_mouse_pos_ = true;
        mouse_last_pos_ = glm::vec2(event->pos().x(), event->pos().y());
        return;
    }

    glm::vec2 mouse_pos = glm::vec2(event->pos().x(), event->pos().y());
    glm::vec2 move_span = mouse_last_pos_ - mouse_pos;
    mouse_last_pos_ = mouse_pos;

    camera_controller_->RotateCamera(move_span);
}

void qe::edit::QuarkWindow::mousePressEvent(QMouseEvent * event)
{
    switch (event->button())
    {
    case Qt::LeftButton:
        break;
    case Qt::RightButton:
    {
        right_button_press_ = true;
        this->setCursor(Qt::BlankCursor);
        break;
    }
    case Qt::MiddleButton:
        break;
    default:
        break;
    }
}

void qe::edit::QuarkWindow::mouseReleaseEvent(QMouseEvent * event)
{
    switch (event->button())
    {
    case Qt::LeftButton:
        break;
    case Qt::RightButton:
    {
        right_button_press_ = false;
        init_mouse_pos_ = false;
        this->setCursor(Qt::ArrowCursor);
        break;
    }
    case Qt::MiddleButton:
        break;
    default:
        break;
    }
}

void qe::edit::QuarkWindow::wheelEvent(QWheelEvent * event)
{
    if (event->angleDelta().y() > 0) {
        camera_controller_->MoveForward();
    }
    else {
        camera_controller_->MoveBack();
    }
}

void qe::edit::QuarkWindow::keyPressEvent(QKeyEvent * event)
{
    switch (event->key())
    {
    case Qt::Key_W: // forward
        camera_controller_->MoveForward();
        break;
    case Qt::Key_S: // back
        camera_controller_->MoveBack();
        break;
    case Qt::Key_A: // left
        camera_controller_->MoveLeft();
        break;
    case Qt::Key_D: // right
        camera_controller_->MoveRight();
        break;
    case Qt::Key_Q: // down
        camera_controller_->MoveDown();
        break;
    case Qt::Key_E: // up
        camera_controller_->MoveUp();
        break;
    default:
        break;
    }
}

void qe::edit::QuarkWindow::keyReleaseEvent(QKeyEvent * event)
{

}

void qe::edit::QuarkWindow::update()
{
    UpdateBehaviour();
    UpdateUniformBuffer();
    Draw();

    frame_count_++;

    if (fps_timer_->elapsed() >= 1000)
    {
        fps_number_ = frame_count_ / ((double)fps_timer_->elapsed() / 1000.0);
    }
}

int qe::edit::QuarkWindow::get_fps()
{
    return fps_number_;
}

void qe::edit::QuarkWindow::LoadScene(std::shared_ptr<qe::core::Scene> scene)
{
    auto roots = scene->get_roots();

    auto standard_shader = std::dynamic_pointer_cast<qe::core::Shader>(resource_->Load(kShaderPath));
    standard_shader->set_global_vector("lightDir", light_dir_);

    auto standard_material = std::make_shared<qe::core::Material>();
    standard_material->set_shader(standard_shader);

    CreateDescriptorSetLayout();

    auto path = standard_material->get_shader()->get_path();
    auto pre_path = qe::core::QuarkString::get_path_prefixed(path);
    auto name = qe::core::QuarkString::get_file_name(path);

    kShaderPre = pre_path + name;

    CreatePipeline();

    CreateUniformBuffer();

    CreateDescriptorSet();

    for (auto root : roots) {
        LoadQuarkObject(root, standard_material);
    }
}

void qe::edit::QuarkWindow::LoadQuarkObject(
    std::shared_ptr<qe::core::QuarkObject> quark_object,
    std::shared_ptr<qe::core::Material> standard_material)
{
    auto mesh_filter = quark_object->get_component<qe::core::MeshFilter>();
    if (!mesh_filter) {
        auto childs = quark_object->get_childs();
        for (auto child : childs) {
            LoadQuarkObject(child, standard_material);
        }

        return;
    }

    auto mesh_renderer = quark_object->add_component<qe::core::MeshRenderer>();
    mesh_renderer->add_material(standard_material);

    auto behaviour = quark_object->add_component<qe::core::AwakeBehaviour>();
    behaviours_.push_back(behaviour);

    LoadDrawData(quark_object->get_transform()->get_local_matrix(), mesh_filter, mesh_renderer);

    auto childs = quark_object->get_childs();
    for (auto child : childs) {
        LoadQuarkObject(child, standard_material);
    }
}

void qe::edit::QuarkWindow::LoadDrawData(
    glm::mat4 m,
    std::shared_ptr<qe::core::MeshFilter> mesh_filter,
    std::shared_ptr<qe::core::MeshRenderer> mesh_renderer)
{
    std::vector<Vertex> vertexs;
    std::vector<uint> indexs;

    GetMeshData(mesh_filter->get_mesh(), vertexs, indexs);

    auto vertex_buffer = std::make_shared<qe::render::vulkan::VulkanBuffer>(sizeof(vertexs[0]) * vertexs.size());
    auto index_buffer = std::make_shared<qe::render::vulkan::VulkanBuffer>(sizeof(indexs[0]) * indexs.size());

    vi_device_->CreateH2DBuffer(vertex_buffer.get(), vertexs.data(),vk::BufferUsageFlagBits::eVertexBuffer);
    vi_device_->CreateH2DBuffer(index_buffer.get(), indexs.data(), vk::BufferUsageFlagBits::eIndexBuffer);

    meshData mesh_data;
    mesh_data.vertexs = vertexs;
    mesh_data.indexs = indexs;
    mesh_data.vertex_buffer = vertex_buffer;
    mesh_data.index_buffer = index_buffer;
    mesh_data.m = m;

    mesh_datas_.push_back(mesh_data);

}

void qe::edit::QuarkWindow::GetMeshData(std::shared_ptr<qe::core::Mesh> mesh, std::vector<Vertex>& vertexs, std::vector<uint>& indexs)
{
    for (int i = 0; i < mesh->get_vertex_count(); i++) {
        Vertex vertex = {};

        vertex.pos = mesh->get_vertexs()->at(i);
        vertex.normal = mesh->get_normals()->at(i);
        vertex.uv = mesh->get_uvs()->at(i);

        vertexs.push_back(vertex);
    }

    for (int j = 0; j < mesh->get_index_count(); j++) {
        indexs.push_back(mesh->get_indexs()->at(j));
    }
}

void qe::edit::QuarkWindow::CreateDescriptorSetLayout()
{
    vk::DescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutBinding uldLayoutBinding = {};
    uldLayoutBinding.binding = 1;
    uldLayoutBinding.descriptorCount = 1;
    uldLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uldLayoutBinding.pImmutableSamplers = nullptr;
    uldLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, uldLayoutBinding };

    vk::DescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VK_CHECK_RESULT(vi_device_->logic_device_.createDescriptorSetLayout(&layoutInfo, nullptr, &descriptor_set_layout_));
}

void qe::edit::QuarkWindow::CreatePipeline()
{
    auto vertShaderPath = kShaderPre + ".vert.spv";
    auto fragShaderPath = kShaderPre + ".frag.spv";

    auto vertShaderModule = qe::render::vulkan::VulkanTools::loadShader(vertShaderPath.c_str(), vi_device_->logic_device_);
    auto fragShaderModule = qe::render::vulkan::VulkanTools::loadShader(fragShaderPath.c_str(), vi_device_->logic_device_);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = false;

    vk::Viewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = vi_device_->swap_chain_extent_.width;
    viewport.height = vi_device_->swap_chain_extent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
   
    vk::Rect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = vi_device_->swap_chain_extent_;
    
    vk::PipelineViewportStateCreateInfo viewportState = {};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
    vk::PipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.depthClampEnable = false;
    rasterizer.rasterizerDiscardEnable = false;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = false;

    vk::PipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sampleShadingEnable = false;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.depthTestEnable = true;
    depthStencil.depthWriteEnable = true;
    depthStencil.depthCompareOp = vk::CompareOp::eLess;
    depthStencil.depthBoundsTestEnable = false;
    depthStencil.stencilTestEnable = false;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = false;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcColor;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcColor;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;

    vk::PipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.logicOpEnable = false;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Push constants for model matrices
    vk::PushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ModelMatrix);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptor_set_layout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK_RESULT(vi_device_->logic_device_.createPipelineLayout(&pipelineLayoutInfo, nullptr, &pipeline_layout_));

    vk::GraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipeline_layout_;
    pipelineInfo.renderPass = vi_device_->render_pass_;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    pipeline_ = vi_device_->logic_device_.createGraphicsPipeline(nullptr, pipelineInfo);

    vi_device_->logic_device_.destroyShaderModule(vertShaderModule);
    vi_device_->logic_device_.destroyShaderModule(fragShaderModule);

}

void qe::edit::QuarkWindow::CreateUniformBuffer()
{
    vi_device_->CreateBuffer(
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        sizeof(UniformCameraBuffer),
        &ubo_buffer_,
        &ubo_buffer_memory_);

    vi_device_->CreateBuffer(
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        sizeof(UniformLightBuffer),
        &uld_buffer_,
        &uld_buffer_memory_);
}

void qe::edit::QuarkWindow::CreateDescriptorSet()
{
    
    std::array<vk::DescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = 1;

    poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[1].descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo = {};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    
    VK_CHECK_RESULT(vi_device_->logic_device_.createDescriptorPool(&poolInfo, nullptr, &descriptor_set_pool_));

    vk::DescriptorSetLayout layouts[] = { descriptor_set_layout_ };
    vk::DescriptorSetAllocateInfo allocInfo = {};
    allocInfo.descriptorPool = descriptor_set_pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    VK_CHECK_RESULT(vi_device_->logic_device_.allocateDescriptorSets(&allocInfo, &descriptor_set_));

    vk::DescriptorBufferInfo uboBufferInfo = {};
    uboBufferInfo.buffer = ubo_buffer_;
    uboBufferInfo.offset = 0;
    uboBufferInfo.range = sizeof(UniformCameraBuffer);

    vk::DescriptorBufferInfo uldBufferInfo = {};
    uldBufferInfo.buffer = uld_buffer_;
    uldBufferInfo.offset = 0;
    uldBufferInfo.range = sizeof(UniformLightBuffer);

    std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {};

    descriptorWrites[0].dstSet = descriptor_set_;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &uboBufferInfo;

    descriptorWrites[1].dstSet = descriptor_set_;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = vk::DescriptorType::eUniformBuffer;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &uldBufferInfo;

    vi_device_->logic_device_.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

}

void qe::edit::QuarkWindow::CreateCommandBuffer()
{
    command_buffers_.resize(vi_device_->swap_chain_frame_buffers_.size());

    vk::CommandBufferAllocateInfo allocInfo = {};
    allocInfo.commandPool = vi_device_->command_pool_;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = (uint32_t)command_buffers_.size();

    VK_CHECK_RESULT(vi_device_->logic_device_.allocateCommandBuffers(&allocInfo, command_buffers_.data()));

    for (size_t i = 0; i < command_buffers_.size(); i++) {
        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

        command_buffers_[i].begin(&beginInfo);

        vk::RenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.renderPass = vi_device_->render_pass_;
        renderPassInfo.framebuffer = vi_device_->swap_chain_frame_buffers_[i];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = vi_device_->swap_chain_extent_;

        vk::ClearColorValue c_color;
        c_color.float32[0] = 0.2f;
        c_color.float32[1] = 0.2f;
        c_color.float32[2] = 0.2f;
        c_color.float32[3] = 1.0f;

        std::array<vk::ClearValue, 2> clearValues = {};
        clearValues[0].color = c_color;
        clearValues[1].depthStencil = { 1.0f, 0 };

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        command_buffers_[i].beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

        for (int index = 0; index < mesh_datas_.size(); index++) {

            command_buffers_[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);

            vk::DeviceSize offsets[] = { 0 };
            command_buffers_[i].bindVertexBuffers(0, 1, &(mesh_datas_[index].vertex_buffer->buffer), offsets);

            command_buffers_[i].bindIndexBuffer(mesh_datas_[index].index_buffer->buffer, 0, vk::IndexType::eUint32);

            command_buffers_[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout_, 0, 1, &descriptor_set_, 0, nullptr);

            glm::mat4 model = mesh_datas_[index].m;
            command_buffers_[i].pushConstants(pipeline_layout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(ModelMatrix), &model);

            command_buffers_[i].drawIndexed(static_cast<uint32_t>(mesh_datas_[index].indexs.size()), 1, 0, 0, 0);
        }

        command_buffers_[i].endRenderPass();

        command_buffers_[i].end();

    }

}

void qe::edit::QuarkWindow::Draw()
{
    uint32_t imageIndex;

    auto acquireResult = vi_device_->logic_device_.acquireNextImageKHR(
        vi_device_->swap_chain_,
        std::numeric_limits<uint64_t>::max(), 
        vi_device_->present_complete_semaphore_,
        vk::Fence(), 
        &imageIndex);

     if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
        RecreateSwapChain();
        return;
    }
    else if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vk::Semaphore waitSemaphores[] = { vi_device_->present_complete_semaphore_ };
    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    vk::SubmitInfo submitInfo;
    submitInfo.setWaitSemaphoreCount(1);
    submitInfo.setPWaitSemaphores(waitSemaphores);
    submitInfo.setPWaitDstStageMask(waitStages);
    submitInfo.setCommandBufferCount(1);
    submitInfo.setPCommandBuffers(&command_buffers_[imageIndex]);

    vk::Semaphore signalSemaphores[] = { vi_device_->render_complete_semaphore_ };
    submitInfo.setSignalSemaphoreCount(1);
    submitInfo.setPSignalSemaphores(signalSemaphores);

    VK_CHECK_RESULT(vi_device_->queue_.submit(1, &submitInfo, vk::Fence()));

    vk::PresentInfoKHR presentInfo = {};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &vi_device_->render_complete_semaphore_;

    vk::SwapchainKHR swapChains[] = { vi_device_->swap_chain_ };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;

    presentInfo.pImageIndices = &imageIndex;

    auto result = vi_device_->queue_.presentKHR(&presentInfo);

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        RecreateSwapChain();
    }
    else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    vi_device_->queue_.waitIdle();

}

void qe::edit::QuarkWindow::UpdateUniformBuffer()
{
    UniformCameraBuffer umo = {};
    umo.proj = camera_controller_->get_p();
    umo.view = camera_controller_->get_v();

    void* data;
    vi_device_->logic_device_.mapMemory(ubo_buffer_memory_, 0, sizeof(umo), vk::MemoryMapFlagBits(), &data);
    memcpy(data, &umo, sizeof(umo));
    vi_device_->logic_device_.unmapMemory(ubo_buffer_memory_);

    UniformLightBuffer ulb = {};
    ulb.lightDir = light_dir_;

    void* light_data;
    vi_device_->logic_device_.mapMemory(uld_buffer_memory_, 0, sizeof(ulb), vk::MemoryMapFlagBits(), &light_data);
    memcpy(light_data, &ulb, sizeof(ulb));
    vi_device_->logic_device_.unmapMemory(uld_buffer_memory_);

}

void qe::edit::QuarkWindow::RecreateSwapChain()
{
    vi_device_->logic_device_.waitIdle();

    CleanSwapChain();

    vi_device_->InitSwapChain();

    vi_device_->CreateRenderPass();

    vi_device_->CreateFrameBuffer();

    CreatePipeline();

    CreateCommandBuffer();
}

void qe::edit::QuarkWindow::CleanSwapChain()
{
    vi_device_->logic_device_.destroyImageView(vi_device_->frame_buffer_depth_image_view_);

    vi_device_->logic_device_.destroyImage(vi_device_->frame_buffer_depth_image_);

    vi_device_->logic_device_.freeMemory(vi_device_->frame_buffer_depth_image_memory_);

    for (int i = 0; i < vi_device_->swap_chain_frame_buffers_.size(); i++) {
        vi_device_->logic_device_.destroyFramebuffer(vi_device_->swap_chain_frame_buffers_[i]);
    }

    vi_device_->logic_device_.freeCommandBuffers(
        vi_device_->command_pool_,
        command_buffers_.size(),
        command_buffers_.data());

    vi_device_->logic_device_.destroyPipelineLayout(pipeline_layout_);

    vi_device_->logic_device_.destroyPipeline(pipeline_);

    vi_device_->logic_device_.destroyRenderPass(vi_device_->render_pass_);

    for (int i = 0; i < vi_device_->swap_chain_image_views_.size(); i++) {
        vi_device_->logic_device_.destroyImageView(vi_device_->swap_chain_image_views_[i]);
    }

    vi_device_->logic_device_.destroySwapchainKHR(vi_device_->swap_chain_);

}

void qe::edit::QuarkWindow::Awake()
{
    for (auto behaviour : behaviours_) {
        behaviour->Awake();
    }
}

void qe::edit::QuarkWindow::UpdateBehaviour()
{
    for (auto behaviour : behaviours_) {
        behaviour->Update();
    }
}
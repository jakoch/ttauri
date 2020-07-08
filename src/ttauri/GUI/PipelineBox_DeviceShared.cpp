// Copyright 2019 Pokitec
// All rights reserved.

#include "PipelineBox.hpp"
#include "PipelineBox_DeviceShared.hpp"
#include "GUIDevice.hpp"
#include "../PixelMap.hpp"
#include "../URL.hpp"
#include <array>

namespace tt::PipelineBox {

using namespace std;

DeviceShared::DeviceShared(GUIDevice const &device) :
    device(device)
{
    buildShaders();
}

DeviceShared::~DeviceShared()
{
}

void DeviceShared::destroy(GUIDevice *vulkanDevice)
{
    tt_assume(vulkanDevice);
    teardownShaders(vulkanDevice);
}

void DeviceShared::drawInCommandBuffer(vk::CommandBuffer &commandBuffer)
{
    commandBuffer.bindIndexBuffer(device.quadIndexBuffer, 0, vk::IndexType::eUint16);
}

void DeviceShared::placeVertices(
    vspan<Vertex> &vertices,
    rect box,
    vec backgroundColor,
    float borderSize,
    vec borderColor,
    vec cornerShapes,
    aarect clippingRectangle
)
{
    ttlet extraSpace = (borderSize * 0.5f) + 1.0f;
    ttlet outerBox = expand(box, extraSpace);

    ttlet v0 = outerBox.corner<0>();
    ttlet v1 = outerBox.corner<1>();
    ttlet v2 = outerBox.corner<2>();
    ttlet v3 = outerBox.corner<3>();

    ttlet outerExtent = outerBox.extent();

    ttlet t0 = outerExtent._00xy();
    ttlet t1 = outerExtent.x00y();
    ttlet t2 = outerExtent._0yx0();
    ttlet t3 = outerExtent.xy00();

    vertices.emplace_back(v0, t0, backgroundColor, borderSize, borderColor, cornerShapes, clippingRectangle);
    vertices.emplace_back(v1, t1, backgroundColor, borderSize, borderColor, cornerShapes, clippingRectangle);
    vertices.emplace_back(v2, t2, backgroundColor, borderSize, borderColor, cornerShapes, clippingRectangle);
    vertices.emplace_back(v3, t3, backgroundColor, borderSize, borderColor, cornerShapes, clippingRectangle);
}

void DeviceShared::buildShaders()
{
    vertexShaderModule = device.loadShader(URL("resource:GUI/PipelineBox.vert.spv"));
    fragmentShaderModule = device.loadShader(URL("resource:GUI/PipelineBox.frag.spv"));

    shaderStages = {
        {vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eVertex, vertexShaderModule, "main"},
        {vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eFragment, fragmentShaderModule, "main"}
    };
}

void DeviceShared::teardownShaders(GUIDevice_vulkan *vulkanDevice)
{
    tt_assume(vulkanDevice);
    vulkanDevice->destroy(vertexShaderModule);
    vulkanDevice->destroy(fragmentShaderModule);
}

}

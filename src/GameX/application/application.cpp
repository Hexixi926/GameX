#include "GameX/application/application.h"

#include "GameX/renderer/renderer.h"

namespace GameX::Base {
Application::Application(const ApplicationSettings &settings)
    : settings_(settings) {
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  if (settings_.width == -1 || settings_.height == -1) {
    if (settings_.fullscreen) {
      // Get the primary monitor
      GLFWmonitor *primaryMonitor = glfwGetPrimaryMonitor();

      // Get the video mode of the primary monitor
      const GLFWvidmode *mode = glfwGetVideoMode(primaryMonitor);

      settings_.width = mode->width;
      settings_.height = mode->height;
    } else {
      settings_.width = 1280;
      settings_.height = 720;
    }
  }

  if (settings_.fullscreen) {
    // Get the primary monitor
    GLFWmonitor *primaryMonitor = glfwGetPrimaryMonitor();

    // Get the video mode of the primary monitor
    const GLFWvidmode *mode = glfwGetVideoMode(primaryMonitor);

    // Set the window to be full screen borderless window
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    // Create a borderless windowed mode window
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  }

  window_ = glfwCreateWindow(settings_.width, settings_.height, "GameX",
                             nullptr, nullptr);

  if (settings_.fullscreen) {
    glfwSetWindowPos(window_, 0, 0);
  }

  grassland::vulkan::CoreSettings core_settings;
  core_settings.window = window_;

  vk_core_ = std::make_unique<grassland::vulkan::Core>(core_settings);

  renderer_ = std::make_unique<class Renderer>(this);

  animation_manager_ = std::make_unique<Animation::Manager>(renderer_.get());

  game_core_ = std::make_unique<Core>(animation_manager_.get());
}

Application::~Application() {
}

void Application::Init() {
  game_core_->Start();
  OnInit();
}

void Application::Cleanup() {
  OnCleanup();
  game_core_->Stop();
  game_core_.reset();
  animation_manager_.reset();
  renderer_.reset();
  vk_core_.reset();
  glfwDestroyWindow(window_);
  glfwTerminate();
}

void Application::Update() {
  OnUpdate();
  static auto last_time = glfwGetTime();
  auto current_time = glfwGetTime();
  auto delta_time = current_time - last_time;
  animation_manager_->Update(delta_time);
  renderer_->SyncObjects();
}

void Application::Render() {
  vk_core_->BeginFrame();

  auto cmd_buffer = vk_core_->CommandBuffer();

  auto frame_image = vk_core_->SwapChain()->Images()[vk_core_->ImageIndex()];

  // Transition frame_image_ to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
  grassland::vulkan::TransitImageLayout(
      cmd_buffer->Handle(), frame_image, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_READ_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

  // Clear frame_image_ to black
  VkClearColorValue clear_color{0.6f, 0.7f, 0.8f, 1.0f};
  VkImageSubresourceRange subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                            1};

  vkCmdClearColorImage(cmd_buffer->Handle(), frame_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1,
                       &subresource_range);

  // Transition frame_image_ to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  grassland::vulkan::TransitImageLayout(
      cmd_buffer->Handle(), frame_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

  if (animation_manager_->Render(cmd_buffer->Handle())) {
    auto film = animation_manager_->PrimaryFilm();
    OutputImage(cmd_buffer->Handle(), film->output_image.get());
  }

  vk_core_->EndFrame();
}

void Application::Run() {
  Init();

  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    Update();
    Render();
  }

  vk_core_->Device()->WaitIdle();
  Cleanup();
}

void Application::OutputImage(VkCommandBuffer cmd_buffer,
                              grassland::vulkan::Image *output_image) {
  auto frame_image = vk_core_->SwapChain()->Images()[vk_core_->ImageIndex()];
  // Blit output_image to frame_image_
  VkImageBlit blit_region = {};
  blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit_region.srcSubresource.layerCount = 1;
  blit_region.srcOffsets[1].x = output_image->Extent().width;
  blit_region.srcOffsets[1].y = output_image->Extent().height;
  blit_region.srcOffsets[1].z = 1;
  blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit_region.dstSubresource.layerCount = 1;
  blit_region.dstOffsets[1].x = vk_core_->SwapChain()->Extent().width;
  blit_region.dstOffsets[1].y = vk_core_->SwapChain()->Extent().height;
  blit_region.dstOffsets[1].z = 1;

  grassland::vulkan::TransitImageLayout(
      cmd_buffer, output_image->Handle(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT);
  grassland::vulkan::TransitImageLayout(
      cmd_buffer, frame_image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT);

  vkCmdBlitImage(cmd_buffer, output_image->Handle(),
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame_image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region,
                 VK_FILTER_NEAREST);

  grassland::vulkan::TransitImageLayout(
      cmd_buffer, output_image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
      VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

  grassland::vulkan::TransitImageLayout(
      cmd_buffer, frame_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}
}  // namespace GameX::Base

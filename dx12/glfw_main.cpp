// Copyright (c) 2020 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#define NOMINMAX            1
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

#include <atlbase.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <array>
#include <iostream>
#include <span>
#include <sstream>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_glfw.h"

#if ENABLE_DX12_DEBUG_LAYER
#  include <dxgidebug.h>
#endif

#define GLFW_EXPOSE_NATIVE_WIN32 1
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

void throw_error(HRESULT r) {
  if(r != S_OK) {
    int err = ::GetLastError();
    std::stringstream msg;
    msg << "Error: hr=" << r << ", GetLastError=" << err;
    throw std::runtime_error(msg.str());
  }
}

class Window {
 public:
  Window(HWND hwnd)
      : hwnd_(hwnd) {
  }

  HWND hwnd() const {
    return hwnd_;
  }

 private:
  HWND hwnd_;
};

class Device {
 public:
  Device() {
#if ENABLE_DX12_DEBUG_LAYER
    CComPtr<ID3D12Debug> dx12_debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12_debug))))
      dx12_debug->EnableDebugLayer();
#endif

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    throw_error(D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&device_)));

#if ENABLE_DX12_DEBUG_LAYER
    if(dx12_debug) {
      CComPtr<ID3D12InfoQueue> info_queue;
      device_->QueryInterface(&info_queue);
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
    }
#endif
    create_graphics_queue();
  }

  ~Device() {
  }

  ID3D12Device* get() const {
    return device_;
  }

  ID3D12CommandQueue* graphics_queue() const {
    return graphics_queue_;
  }

  template <typename OutputIterator>
  void cache_descriptor_handles(ID3D12DescriptorHeap* heap, OutputIterator out) {
    auto desc = heap->GetDesc();
    auto size = device_->GetDescriptorHandleIncrementSize(desc.Type);
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    for(int i = 0; i < desc.NumDescriptors; ++i) {
      *out++ = handle;
      handle.ptr += size;
    }
  }

 private:
  void create_graphics_queue() {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    throw_error(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&graphics_queue_)));
  }

  CComPtr<ID3D12Device> device_;
  CComPtr<ID3D12CommandQueue> graphics_queue_;
};

class RenderContext {
 public:
  RenderContext() {
  }

  void set_image(D3D12_CPU_DESCRIPTOR_HANDLE desc, ID3D12Resource* image) {
    image_view_ = desc;
    image_ = image;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE const& image_descriptor() const {
    return image_view_;
  }

  ID3D12Resource* image() const {
    return image_;
  }

  void present_fence_value(UINT64 value) {
    present_fence_value_ = value;
  }

  UINT64 present_fence_value() const {
    return present_fence_value_;
  }

 private:
  D3D12_CPU_DESCRIPTOR_HANDLE image_view_ = {};
  ID3D12Resource* image_ = nullptr;
  UINT64 present_fence_value_ = 0;
};

class SubmissionContext {
 public:
  SubmissionContext(Device& device)
      : device_(device) {
    throw_error(device_.get()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&command_allocator_)));
  }

  ID3D12CommandAllocator* command_allocator() const {
    return command_allocator_;
  }

 private:
  Device& device_;
  CComPtr<ID3D12CommandAllocator> command_allocator_;
};

class Swapchain {
 public:
  Swapchain(Device& device, Window& window, int num_images)
      : device_(device)
      , window_(window)
      , num_images_(num_images) {
    create_swapchain();
    create_image_view();
    create_images();
    create_fence();
  }

  ~Swapchain() {
    if(present_fence_event_) {
      CloseHandle(present_fence_event_);
    }
  }

  void present(RenderContext& rc) {
    swapchain_->Present(1, 0); // With vsync
    UINT64 signal_value = current_present_fence_value_ + 1;
    device_.graphics_queue()->Signal(present_fence_, signal_value);
    current_present_fence_value_ = signal_value;
    rc.present_fence_value(current_present_fence_value_);
  }

  UINT get_current_image_index() const {
    return swapchain_->GetCurrentBackBufferIndex();
  }

  D3D12_CPU_DESCRIPTOR_HANDLE const& image_descriptor(int idx) const {
    return image_view_[idx];
  }

  ID3D12Resource* image(int idx) const {
    return image_[idx];
  }

  void wait(RenderContext& rc) {
    std::array<HANDLE, 2> waitables = {swapchain_waitable_};
    DWORD num_waitables = 1;

    // Can be zero if no fence is signaled.
    auto fence_value = rc.present_fence_value();
    if(fence_value) {
      rc.present_fence_value(0);
      present_fence_->SetEventOnCompletion(fence_value, present_fence_event_);
      waitables[1] = present_fence_event_;
      num_waitables = 2;
    }

    WaitForMultipleObjects(num_waitables, waitables.data(), TRUE, INFINITE);
  }

 private:
  void create_swapchain() {
    DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
    sc_desc.BufferCount = num_images_;
    sc_desc.Width = 0;
    sc_desc.Height = 0;
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.SampleDesc.Quality = 0;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sc_desc.Scaling = DXGI_SCALING_STRETCH;
    sc_desc.Stereo = FALSE;

    CComPtr<IDXGIFactory4> factory;
    throw_error(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    CComPtr<IDXGISwapChain1> swapchain;
    throw_error(factory->CreateSwapChainForHwnd(
        device_.graphics_queue(),
        window_.hwnd(),
        &sc_desc,
        nullptr,
        nullptr,
        &swapchain));

    throw_error(swapchain->QueryInterface(&swapchain_));
    swapchain_->SetMaximumFrameLatency(num_images_);
    swapchain_waitable_ = swapchain_->GetFrameLatencyWaitableObject();
  }

  void create_image_view() {
    auto* device = device_.get();
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = num_images_;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    throw_error(
        device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&image_view_heap_)));
    device_.cache_descriptor_handles(image_view_heap_, image_view_.begin());
  }

  void create_images() {
    for(int i = 0; i < num_images_; ++i) {
      CComPtr<ID3D12Resource> image;
      swapchain_->GetBuffer(i, IID_PPV_ARGS(&image));
      device_.get()->CreateRenderTargetView(image, nullptr, image_view_[i]);
      image_[i] = std::move(image);
    }
  }

  void create_fence() {
    throw_error(device_.get()->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&present_fence_)));
    current_present_fence_value_ = 0;
    present_fence_event_ = CreateEvent(NULL, FALSE, FALSE, NULL);
  }

  Device& device_;
  Window& window_;
  int num_images_;
  CComPtr<IDXGISwapChain3> swapchain_;
  CComPtr<ID3D12DescriptorHeap> image_view_heap_;
  CComPtr<ID3D12Fence> present_fence_;
  UINT64 current_present_fence_value_ = 0;
  HANDLE present_fence_event_ = nullptr;
  HANDLE swapchain_waitable_ = nullptr;
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> image_view_;
  std::array<CComPtr<ID3D12Resource>, 3> image_;
};

class ImGuiState {
 public:
  ImGuiState(
      Device& device,
      GLFWwindow* window,
      ID3D12CommandAllocator* command_allocator,
      int num_swapchain_images)
      : device_(device) {
    create_image_descriptors();
    create_command_list(command_allocator);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable
    // Keyboard Controls io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; //
    // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    // Hijjacking vulkan glfw for dx12 seems to work
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Setup Platform/Renderer backends
    ImGui_ImplDX12_Init(
        device.get(),
        num_swapchain_images,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        descriptor_heap_,
        descriptor_heap_->GetCPUDescriptorHandleForHeapStart(),
        descriptor_heap_->GetGPUDescriptorHandleForHeapStart());
  }

  ~ImGuiState() {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  void render(SubmissionContext& sc, RenderContext& rc) {
    sc.command_allocator()->Reset();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = rc.image();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list_->Reset(sc.command_allocator(), NULL);
    command_list_->ResourceBarrier(1, &barrier);

    command_list_->OMSetRenderTargets(1, &rc.image_descriptor(), FALSE, nullptr);

    command_list_->SetDescriptorHeaps(1, &descriptor_heap_.p);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list_);

    // cglover-todo: This should move out of here
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->Close();

    std::array<ID3D12CommandList*, 1> commands = {command_list_.p};
    device_.graphics_queue()->ExecuteCommandLists(commands.size(), commands.data());
  }

 private:
  void create_image_descriptors() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    throw_error(
        device_.get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap_)));
  }

  void create_command_list(ID3D12CommandAllocator* command_allocator) {
    throw_error(device_.get()->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        command_allocator, // Only used for create.
        nullptr,
        IID_PPV_ARGS(&command_list_)));
    throw_error(command_list_->Close());
  }

  Device& device_;
  CComPtr<ID3D12DescriptorHeap> descriptor_heap_;
  CComPtr<ID3D12GraphicsCommandList> command_list_;
};

bool g_swapchain_rebuild_required = false;

static void glfw_error_callback(int error, const char* description) {
  std::cerr << "Glfw Error " << error << ": " << description;
}

int main(int, char**) {
  // Setup GLFW window
  glfwSetErrorCallback(glfw_error_callback);
  if(!glfwInit())
    return 1;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1280, 720, "rndrx-dx12", NULL, NULL);

  Device device;

  HWND hwnd = glfwGetWin32Window(window);
  Window wnd(hwnd);

  int num_swapchain_images = 3;
  Swapchain swapchain(device, wnd, num_swapchain_images);

  std::vector<SubmissionContext> submission_context_list;

  int num_frame_in_flight = 3;
  submission_context_list.resize(num_frame_in_flight, device);

  ImGuiState imgui(
      device,
      window,
      submission_context_list[0].command_allocator(),
      num_swapchain_images);

  std::vector<RenderContext> render_context_list;
  render_context_list.resize(num_swapchain_images);
  for(int i = 0; i < num_swapchain_images; ++i) {
    auto& rc = render_context_list[i];
    rc.set_image(swapchain.image_descriptor(i), swapchain.image(i));
  }

  // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can
  // also load multiple fonts and use ImGui::PushFont()/PopFont() to select
  // them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you
  // need to select the font among multiple.
  // - If the file cannot be loaded, the function will return NULL. Please
  // handle those errors in your application (e.g. use an assertion, or
  // display an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and
  // stored into a texture when calling
  // ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame
  // below will call.
  // - Read 'docs/FONTS.md' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string
  // literal you need to write a double backslash \\ !
  // io.Fonts->AddFontDefault();
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
  // ImFont* font =
  // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f,
  // NULL, io.Fonts->GetGlyphRangesJapanese()); IM_ASSERT(font != NULL);

  // Upload Fonts
  {
    // Use any command queue
    // VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;
    // VkCommandBuffer command_buffer = wd->Frames[wd->FrameIndex].CommandBuffer;

    // err = vkResetCommandPool(g_Device, command_pool, 0);
    // check_vk_result(err);
    // VkCommandBufferBeginInfo begin_info = {};
    // begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    // err = vkBeginCommandBuffer(command_buffer, &begin_info);
    // check_vk_result(err);

    // ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

    // VkSubmitInfo end_info = {};
    // end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    // end_info.commandBufferCount = 1;
    // end_info.pCommandBuffers = &command_buffer;
    // err = vkEndCommandBuffer(command_buffer);
    // check_vk_result(err);
    // err = vkQueueSubmit(g_Queue, 1, &end_info, VK_NULL_HANDLE);
    // check_vk_result(err);

    // err = vkDeviceWaitIdle(g_Device);
    // check_vk_result(err);
    // ImGui_ImplVulkan_DestroyFontUploadObjects();
  }

  // Our state
  bool show_demo_window = true;
  bool show_another_window = false;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  std::uint32_t frame_index = 0;
  // Main loop
  while(!glfwWindowShouldClose(window)) {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
    // tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
    // your main application.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
    // data to your main application. Generally you may always pass all inputs
    // to dear imgui, and hide them from your application based on those two
    // flags.
    glfwPollEvents();

    // Resize swap chain?
    if(g_swapchain_rebuild_required) {
      int width, height;
      glfwGetFramebufferSize(window, &width, &height);
      if(width > 0 && height > 0) {
        // ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
        // ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice,
        // g_Device, &g_MainWindowData, g_QueueFamily, g_Allocator, width,
        // height, g_MinImageCount); g_MainWindowData.FrameIndex = 0;
        g_swapchain_rebuild_required = false;
      }
    }

    std::uint32_t next_frame_index = frame_index++;
    SubmissionContext& submission_context =
        submission_context_list[next_frame_index % submission_context_list.size()];

    RenderContext& render_context =
        render_context_list[swapchain.get_current_image_index()];

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 1. Show the big demo window (Most of the sample code is in
    // ImGui::ShowDemoWindow()! You can browse its code to learn more about
    // Dear ImGui!).
    if(show_demo_window)
      ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End
    // pair to created a named window.
    {
      static float f = 0.0f;
      static int counter = 0;

      // Create a window called "Hello, world!" and append into it.
      ImGui::Begin("Hello, world!");
      // Display some text (you can use a format strings too)
      ImGui::Text("This is some useful text.");
      // Edit bools storing our window open/close state
      ImGui::Checkbox("Demo Window", &show_demo_window);
      ImGui::Checkbox("Another Window", &show_another_window);
      // Edit 1 float using a slider from 0.0f to 1.0f
      ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
      // Edit 3 floats representing a color
      ImGui::ColorEdit3("clear color", (float*)&clear_color);
      // Buttons return true when clicked (most widgets return true when
      // edited/activated)
      if(ImGui::Button("Button"))
        counter++;
      ImGui::SameLine();
      ImGui::Text("counter = %d", counter);

      ImGui::Text(
          "Application average %.3f ms/frame (%.1f FPS)",
          1000.0f / ImGui::GetIO().Framerate,
          ImGui::GetIO().Framerate);
      ImGui::End();
    }

    // 3. Show another simple window.
    if(show_another_window) {
      ImGui::Begin(
          "Another Window",
          &show_another_window); // Pass a pointer to our bool variable (the
                                 // window will have a closing button that
                                 // will clear the bool when clicked)
      ImGui::Text("Hello from another window!");
      if(ImGui::Button("Close Me"))
        show_another_window = false;
      ImGui::End();
    }

    // Rendering
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    bool const is_minimized =
        (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if(!is_minimized) {
      swapchain.wait(render_context);
      // memcpy(&wd->ClearValue.color.float32[0], &clear_color, 4 *sizeof(float));

      imgui.render(submission_context, render_context);
      swapchain.present(render_context);
    }
  }

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}

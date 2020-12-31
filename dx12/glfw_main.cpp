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
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <array>
#include <codecvt>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_glfw.h"

#if RNDRX_ENABLE_DX12_DEBUG_LAYER
#  include <dxgidebug.h>
#endif

#define GLFW_EXPOSE_NATIVE_WIN32 1
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

struct noncopyable {
  noncopyable() = default;
  noncopyable(noncopyable const&) = delete;
  noncopyable& operator=(noncopyable const&) = delete;
  noncopyable(noncopyable&&) = default;
  noncopyable& operator=(noncopyable&&) = default;
};

enum class LogLevel {
  None,
  Error,
  Warn,
  Info,
  Trace,
  NumLogLevels,
};

LogLevel const g_LogLevel = LogLevel::Trace;
struct LogState : noncopyable {
  LogState(std::ostream& os)
      : os_(os) {
  }

  bool is_done() const {
    return done_;
  }

  void done() {
    done_ = true;
    os_ << std::endl;
  }

  std::ostream& os_;
  bool done_ = false;
};

#define LOG(level)                                                             \
  for(LogState log_state(std::cerr);                                           \
      g_LogLevel >= LogLevel::level && !log_state.is_done();                   \
      log_state.done())                                                        \
  std::cerr

void throw_error(HRESULT r) {
  if(r != S_OK) {
    int err = ::GetLastError();
    std::stringstream msg;
    msg << "Error: hr=" << r << ", GetLastError=" << err;
    throw std::runtime_error(msg.str());
  }
}

std::string utf16_to_utf8(std::wstring const& utf16Str) {
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
  return conv.to_bytes(utf16Str);
}

class Window : noncopyable {
 public:
  Window() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width_, height_, "rndrx-dx12", NULL, NULL);
    hwnd_ = glfwGetWin32Window(window_);
  }

  ~Window() {
    glfwDestroyWindow(window_);
  }

  GLFWwindow* glfw() const {
    return window_;
  }

  HWND hwnd() const {
    return hwnd_;
  }

  int width() const {
    return width_;
  }

  int height() const {
    return height_;
  }

  enum class SizeEvent {
    None,
    Changed,
  };

  SizeEvent handle_window_size() {
    int old_width = width_;
    int old_height = height_;
    glfwGetFramebufferSize(window_, &width_, &height_);
    if(width_ != old_width || height_ != old_height) {
      return SizeEvent::Changed;
    }

    return SizeEvent::None;
  }

 private:
  GLFWwindow* window_ = nullptr;
  HWND hwnd_ = nullptr;
  int width_ = 1920;
  int height_ = 1080;
};

class Device : noncopyable {
 public:
  Device(IDXGIAdapter* adapter)
      : adapter_(adapter) {
#if RNDRX_ENABLE_DX12_DEBUG_LAYER
    CComPtr<ID3D12Debug> dx12_debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12_debug)))) {
      dx12_debug->EnableDebugLayer();
    }
#endif

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    throw_error(D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&device_)));

#if RNDRX_ENABLE_DX12_DEBUG_LAYER
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

  Device(Device const&) = delete;
  Device& operator=(Device const&) = delete;

  ID3D12Device4* get() const {
    return device_;
  }

  ID3D12CommandQueue* graphics_queue() const {
    return graphics_queue_;
  }

  IDXGIAdapter* adapter() const {
    return adapter_;
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

  CComPtr<ID3D12Device4> device_;
  CComPtr<ID3D12CommandQueue> graphics_queue_;
  IDXGIAdapter* adapter_ = nullptr;
};

class SubmissionContext : noncopyable {
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

  void present_fence_value(UINT64 value) {
    present_fence_value_ = value;
  }

  UINT64 present_fence_value() const {
    return present_fence_value_;
  }

  void begin_frame() {
    throw_error(command_allocator()->Reset());
  }

  void begin_rendering(ID3D12GraphicsCommandList* command_list) {
    command_list->Reset(command_allocator(), nullptr);
    command_list_ = command_list;
  }

  void finish_rendering() {
    throw_error(command_list_->Close());
    std::array<ID3D12CommandList*, 1> commands = {command_list_};
    device_.graphics_queue()->ExecuteCommandLists(commands.size(), commands.data());
    command_list_ = nullptr;
  }

  ID3D12GraphicsCommandList* command_list() const {
    return command_list_;
  }

 private:
  Device& device_;
  CComPtr<ID3D12CommandAllocator> command_allocator_;
  UINT64 present_fence_value_ = 0;
  ID3D12GraphicsCommandList* command_list_;
};

class RenderContext : noncopyable {
 public:
  void image(ID3D12Resource* image) {
    image_ = image;
  }

  ID3D12Resource* image() const {
    return image_;
  }

  void image_view(D3D12_CPU_DESCRIPTOR_HANDLE image_view) {
    image_view_ = image_view;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE image_view() const {
    return image_view_;
  }

  void viewport(int width, int height) {
    viewport_.Width = width;
    viewport_.Height = height;
    viewport_.TopLeftY = 0.0f;
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.MinDepth = D3D12_MIN_DEPTH;
    viewport_.MaxDepth = D3D12_MAX_DEPTH;
  }

  D3D12_VIEWPORT const& viewport() const {
    return viewport_;
  }

  void scissor(int left, int right, int top, int bottom) {
    scissor_.left = left;
    scissor_.right = right;
    scissor_.top = top;
    scissor_.bottom = bottom;
  }

  D3D12_RECT const& scissor() const {
    return scissor_;
  }

 private:
  D3D12_CPU_DESCRIPTOR_HANDLE image_view_ = {};
  ID3D12Resource* image_ = nullptr;

  D3D12_VIEWPORT viewport_;
  D3D12_RECT scissor_;
};

class Swapchain : noncopyable {
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

    if(swapchain_waitable_) {
      CloseHandle(swapchain_waitable_);
    }
  }

  int image_count() const {
    return num_images_;
  }

  UINT get_current_image_index() const {
    return swapchain_->GetCurrentBackBufferIndex();
  }

  D3D12_CPU_DESCRIPTOR_HANDLE const& image_descriptor(int idx) const {
    return image_view_[idx];
  }

  void present(SubmissionContext& sc) {
    swapchain_->Present(1, 0); // 1=With vsync
    UINT64 signal_value = ++current_present_fence_value_;
    throw_error(device_.graphics_queue()->Signal(present_fence_, signal_value));
    sc.present_fence_value(signal_value);
  }

  ID3D12Resource* image(int idx) const {
    return image_[idx];
  }

  void wait(SubmissionContext& sc) {
    auto completed_value = present_fence_->GetCompletedValue();
    auto fence_value = sc.present_fence_value();
    if(completed_value < fence_value) {
      throw_error(
          present_fence_->SetEventOnCompletion(fence_value, present_fence_event_));
      WaitForSingleObject(present_fence_event_, INFINITE);
    }
  }

  void wait_for_last_frame() {
    auto completed_value = present_fence_->GetCompletedValue();
    auto fence_value = current_present_fence_value_;

    // Can be zero if no fence is signaled.
    if(completed_value < fence_value) {
      throw_error(
          present_fence_->SetEventOnCompletion(fence_value, present_fence_event_));
      WaitForSingleObject(present_fence_event_, INFINITE);
      LOG(Info) << "Waited for value: " << present_fence_->GetCompletedValue();
    }
  }

  void resize_swapchain(int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    swapchain_->GetDesc1(&desc);
    desc.Width = width;
    desc.Height = height;

    for(int i = 0; i < num_images_; ++i) {
      image_[i] = nullptr;
    }

    swapchain_ = nullptr;
    CloseHandle(swapchain_waitable_);
    create_swapchain_impl(desc);
    create_images();
  }

 private:
  void create_swapchain() {
    DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
    sc_desc.BufferCount = num_images_;
    sc_desc.Width = window_.width();
    sc_desc.Height = window_.height();
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.SampleDesc.Quality = 0;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sc_desc.Scaling = DXGI_SCALING_STRETCH;
    sc_desc.Stereo = FALSE;

    create_swapchain_impl(sc_desc);
  }

  void create_swapchain_impl(DXGI_SWAP_CHAIN_DESC1 const& desc) {
    CComPtr<IDXGIFactory4> factory;
    throw_error(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    CComPtr<IDXGISwapChain1> swapchain;
    throw_error(factory->CreateSwapChainForHwnd(
        device_.graphics_queue(),
        window_.hwnd(),
        &desc,
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
    auto* device = device_.get();
    for(int i = 0; i < num_images_; ++i) {
      CComPtr<ID3D12Resource> image;
      swapchain_->GetBuffer(i, IID_PPV_ARGS(&image));
      device->CreateRenderTargetView(image, nullptr, image_view_[i]);
      image_[i] = std::move(image);
    }
  }

  void create_fence() {
    throw_error(device_.get()->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&present_fence_)));
    current_present_fence_value_ = 1;
    present_fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if(!present_fence_event_) {
      throw_error(HRESULT_FROM_WIN32(GetLastError()));
    }
  }

  Device& device_;
  Window& window_;
  int num_images_ = 0;
  int width_ = 0;
  int height_ = 0;
  CComPtr<IDXGISwapChain3> swapchain_;
  CComPtr<ID3D12DescriptorHeap> image_view_heap_;
  CComPtr<ID3D12Fence> present_fence_;
  UINT64 current_present_fence_value_ = 0;
  HANDLE present_fence_event_ = nullptr;
  HANDLE swapchain_waitable_ = nullptr;
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> image_view_ = {};
  std::array<CComPtr<ID3D12Resource>, 3> image_;
};

class ImGuiState : noncopyable {
 public:
  ImGuiState(Device& device, Window& window, int num_swapchain_images)
      : device_(device) {
    create_shader_descriptor();
    create_image_descriptor();
    create_image(window.width(), window.height());
    create_command_list();

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
    ImGui_ImplGlfw_InitForVulkan(window.glfw(), true);

    // Setup Platform/Renderer backends
    ImGui_ImplDX12_Init(
        device.get(),
        num_swapchain_images,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        shader_heap_,
        shader_heap_->GetCPUDescriptorHandleForHeapStart(),
        shader_heap_->GetGPUDescriptorHandleForHeapStart());
  }

  ~ImGuiState() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  void create_image(int width, int height) {
    image_ = nullptr;
    D3D12_RESOURCE_DESC desc = {};
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Width = width;
    desc.Height = height;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    auto* device = device_.get();
    D3D12_HEAP_PROPERTIES heap = device->GetCustomHeapProperties(
        0,
        D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::copy(&clear_colour_.x, &clear_colour_.x + 4, &clear.Color[0]);

    throw_error(device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        &clear,
        IID_PPV_ARGS(&image_)));

    device->CreateRenderTargetView(image_, nullptr, image_view_);
  }

  void update() {
    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 1. Show the big demo window (Most of the sample code is in
    // ImGui::ShowDemoWindow()! You can browse its code to learn more about
    // Dear ImGui!).
    if(show_demo_window_)
      ImGui::ShowDemoWindow(&show_demo_window_);

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
      ImGui::Checkbox("Demo Window", &show_demo_window_);
      ImGui::Checkbox("Another Window", &show_another_window_);
      // Edit 1 float using a slider from 0.0f to 1.0f
      ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
      // Edit 3 floats representing a color
      ImGui::ColorEdit3("clear color", reinterpret_cast<float*>(&clear_colour_));
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
    if(show_another_window_) {
      // Pass a pointer to our bool variable (the window will have a closing
      // button that will clear the bool when clicked)
      ImGui::Begin("Another Window", &show_another_window_);
      ImGui::Text("Hello from another window!");
      if(ImGui::Button("Close Me"))
        show_another_window_ = false;
      ImGui::End();
    }
  }

  void render(SubmissionContext& sc) {
    ImGui::Render();
    throw_error(command_list_->Reset(sc.command_allocator(), NULL));

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = image_;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list_->ResourceBarrier(1, &barrier);

    command_list_->ClearRenderTargetView(image_view_, &clear_colour_.x, 0, nullptr);
    command_list_->OMSetRenderTargets(1, &image_view_, FALSE, nullptr);
    command_list_->SetDescriptorHeaps(1, &shader_heap_.p);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list_);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    throw_error(command_list_->Close());

    std::array<ID3D12CommandList*, 1> commands = {command_list_.p};
    device_.graphics_queue()->ExecuteCommandLists(commands.size(), commands.data());
  }

  ID3D12Resource* image() const {
    return image_;
  }

 private:
  void create_shader_descriptor() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    auto* device = device_.get();
    throw_error(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&shader_heap_)));
  }

  void create_image_descriptor() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    auto* device = device_.get();
    throw_error(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&image_heap_)));
    device_.cache_descriptor_handles(image_heap_, &image_view_);
  }

  void create_command_list() {
    auto* device = device_.get();
    throw_error(device->CreateCommandList1(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&command_list_)));
  }

  Device& device_;
  CComPtr<ID3D12DescriptorHeap> shader_heap_;
  CComPtr<ID3D12DescriptorHeap> image_heap_;
  CComPtr<ID3D12GraphicsCommandList> command_list_;
  D3D12_CPU_DESCRIPTOR_HANDLE image_view_ = {};
  CComPtr<ID3D12Resource> image_;
  bool show_demo_window_ = false;
  bool show_another_window_ = false;
  ImVec4 clear_colour_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
};

class FullscreenPass : noncopyable {
 public:
  FullscreenPass(Device& d, Window& w) {
    create_root_signature(d);
    create_pipeline(d);
    create_vertex_buffer(d, w);
  }

  void render(RenderContext& rc, SubmissionContext& sc) {
    auto* command_list = sc.command_list();
    command_list->SetGraphicsRootSignature(root_signature_);

    command_list->RSSetViewports(1, &rc.viewport());
    command_list->RSSetScissorRects(1, &rc.scissor());
    command_list->OMSetRenderTargets(1, &rc.image_view(), FALSE, nullptr);
    command_list->SetPipelineState(pipeline_);

    // glm::vec4 clear_colour(0.5f, 0.2f, 0.4f, 0.f);
    // command_list->ClearRenderTargetView(rc.image_view(), &clear_colour[0], 0,
    // nullptr);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view_);
    command_list->DrawInstanced(3, 1, 0, 0);
  }

 private:
  struct Vertex {
    glm::vec3 position;
    glm::vec4 colour;
  };

  void create_root_signature(Device& d) {
    auto* device = d.get();
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    throw_error(D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error));
    throw_error(device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&root_signature_)));
  }

  void create_pipeline(Device& d) {
    auto* device = d.get();
    CComPtr<ID3DBlob> vertex_shader;
    CComPtr<ID3DBlob> pixel_shader;

#if RNDRX_ENABLE_SHADER_DEBUGGING
    // Enable better shader debugging with the graphics debugging tools.
    unsigned compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    unsigned compile_flags = 0;
#endif

    // clang-format off
    throw_error(D3DCompileFromFile(
        L"shaders.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0", compile_flags,
        0, &vertex_shader, nullptr));
    throw_error(D3DCompileFromFile(
        L"shaders.hlsl", nullptr, nullptr,
        "PSMain", "ps_5_0", compile_flags,
        0, &pixel_shader, nullptr));

    D3D12_INPUT_ELEMENT_DESC vertex_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    // clang-format on

    D3D12_SHADER_BYTECODE vs_bytecode = {};
    vs_bytecode.pShaderBytecode = vertex_shader->GetBufferPointer();
    vs_bytecode.BytecodeLength = vertex_shader->GetBufferSize();

    D3D12_SHADER_BYTECODE ps_bytecode = {};
    ps_bytecode.pShaderBytecode = pixel_shader->GetBufferPointer();
    ps_bytecode.BytecodeLength = pixel_shader->GetBufferSize();

    D3D12_RASTERIZER_DESC raster_desc = {};
    raster_desc.FillMode = D3D12_FILL_MODE_SOLID;
    raster_desc.CullMode = D3D12_CULL_MODE_BACK;
    raster_desc.FrontCounterClockwise = FALSE;
    raster_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster_desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster_desc.DepthClipEnable = TRUE;
    raster_desc.MultisampleEnable = FALSE;
    raster_desc.AntialiasedLineEnable = FALSE;
    raster_desc.ForcedSampleCount = 0;
    raster_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blend_desc = {};
    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
    blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {vertex_layout, _countof(vertex_layout)};
    pso_desc.pRootSignature = root_signature_;
    pso_desc.VS = vs_bytecode;
    pso_desc.PS = ps_bytecode;
    pso_desc.RasterizerState = raster_desc;
    pso_desc.BlendState = blend_desc;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;
    throw_error(
        device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_)));
  }

  void create_vertex_buffer(Device& d, Window& w) {
    auto* device = d.get();
    float aspect_ratio = static_cast<float>(w.width()) / w.height();
    // Define the geometry for a triangle.

    std::array<Vertex, 3> vertices = {};
    // float size = 0.5f;
    // vertices[0].position = glm::vec3(0.0f, size * aspect_ratio, 0.0f);
    // vertices[0].colour = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    // vertices[1].position = glm::vec3(size, -size * aspect_ratio, 0.0f);
    // vertices[1].colour = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    // vertices[2].position = glm::vec3(-size, -size * aspect_ratio, 0.0f);
    // vertices[2].colour = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    // float size = 0.5f;
    vertices[0].position = glm::vec3(0.0f, 1.f, 0.0f);
    vertices[0].colour = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    vertices[1].position = glm::vec3(1.f, -1.f, 0.0f);
    vertices[1].colour = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    vertices[2].position = glm::vec3(-1.f, -1.f, 0.0f);
    vertices[2].colour = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeof(vertices);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    // Note: using upload heaps to transfer static data like vert buffers is
    // not recommended. Every time the GPU needs it, the upload heap will be
    // marshalled over. Please read up on Default Heap usage. An upload heap
    // is used here for code simplicity and because there are very few verts
    // to actually transfer.
    D3D12_HEAP_PROPERTIES heap = device->GetCustomHeapProperties(
        0,
        D3D12_HEAP_TYPE_UPLOAD);
    throw_error(device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertex_buffer_)));

    D3D12_RANGE read_range;
    read_range.Begin = 0;
    read_range.End = 0;
    UINT8* buffer = nullptr;
    throw_error(
        vertex_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&buffer)));

    std::memcpy(buffer, vertices.data(), sizeof(vertices));
    vertex_buffer_->Unmap(0, nullptr);

    vertex_buffer_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
    vertex_buffer_view_.StrideInBytes = sizeof(Vertex);
    vertex_buffer_view_.SizeInBytes = sizeof(vertices);
  }

  CComPtr<ID3D12RootSignature> root_signature_;
  CComPtr<ID3D12PipelineState> pipeline_;
  CComPtr<ID3D12Resource> vertex_buffer_;
  D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_;
};

std::vector<CComPtr<IDXGIAdapter>> get_adapters() {
  CComPtr<IDXGIFactory4> factory;
  throw_error(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

  std::vector<CComPtr<IDXGIAdapter>> adapters;
  CComPtr<IDXGIAdapter> adapter;
  for(int i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    adapters.push_back(std::move(adapter));
  }

  return adapters;
}

class Application : noncopyable {
 public:
  Application(Window& window)
      : window_(window) {
    save_adapters();
  }

  bool run() {
    LOG(Info) << "Running render loop";
    Device device(adapters_[adapter_index_]);

    int num_swapchain_images = 3;
    Swapchain swapchain(device, window_, num_swapchain_images);

    std::vector<SubmissionContext> submission_context_list;

    int num_frames_in_flight = 3;
    for(int i = 0; i < num_frames_in_flight; ++i) {
      submission_context_list.emplace_back(device);
    }

    ImGuiState imgui(device, window_, swapchain.image_count());

    std::vector<RenderContext> render_context_list;
    render_context_list.resize(swapchain.image_count());
    for(std::size_t i = 0; i < render_context_list.size(); ++i) {
      auto&& rc = render_context_list[i];
      rc.image(swapchain.image(i));
      rc.image_view(swapchain.image_descriptor(i));
      rc.scissor(0, window_.width(), 0, window_.height());
      rc.viewport(window_.width(), window_.height());
    }

    FullscreenPass triangle(device, window_);

    CComPtr<ID3D12GraphicsCommandList> command_list;
    throw_error(device.get()->CreateCommandList1(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&command_list)));

    std::uint32_t frame_index = 0;
    while(!glfwWindowShouldClose(window_.glfw())) {
      // Poll and handle events (inputs, window resize, etc.)
      // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags
      // to tell if dear imgui wants to use your inputs.
      // - When io.WantCaptureMouse is true, do not dispatch mouse input
      // data to your main application.
      // - When io.WantCaptureKeyboard is true, do not dispatch keyboard
      // input data to your main application. Generally you may always pass
      // all inputs to dear imgui, and hide them from your application based
      // on those two flags.
      glfwPollEvents();
      if(handle_window_size(swapchain, render_context_list) ==
         Window::SizeEvent::Changed) {
        imgui.create_image(window_.width(), window_.height());
      }

      imgui.update();

      if(ImGui::Begin("Adapter Info")) {
        int selected_index = adapter_index_;
        ImGui::Combo("##name", &selected_index, adapter_names_.data());
        ImGui::End();
        if(selected_index != adapter_index_) {
          adapter_index_ = selected_index;
          LOG(Info) << "Adapter switch detected.";
          swapchain.wait_for_last_frame();
          // Return true unwinds the stack, cleaning everythign up, and
          // then calls run again.
          return true;
        }
      }

      std::uint32_t next_frame_index = frame_index++;
      SubmissionContext& submission_context =
          submission_context_list[next_frame_index % submission_context_list.size()];

      RenderContext& render_context =
          render_context_list[swapchain.get_current_image_index()];

      swapchain.wait(submission_context);
      submission_context.begin_frame();
      imgui.render(submission_context);

      submission_context.begin_rendering(command_list);
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = render_context.image();
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      command_list->ResourceBarrier(1, &barrier);

      D3D12_TEXTURE_COPY_LOCATION src = {};
      src.pResource = imgui.image();
      src.SubresourceIndex = 0;

      D3D12_TEXTURE_COPY_LOCATION dest = {};
      dest.pResource = render_context.image();
      dest.SubresourceIndex = 0;
      command_list->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
      command_list->ResourceBarrier(1, &barrier);

      triangle.render(render_context, submission_context);

      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
      command_list->ResourceBarrier(1, &barrier);
      submission_context.finish_rendering();
      swapchain.present(submission_context);
    }

    swapchain.wait_for_last_frame();
    return false;
  }

 private:
  void save_adapters() {
    adapters_ = get_adapters();
    for(auto&& adapter : adapters_) {
      DXGI_ADAPTER_DESC desc = {};
      adapter->GetDesc(&desc);
      auto narrow_name = utf16_to_utf8(desc.Description);
      adapter_names_.insert(
          adapter_names_.end(),
          narrow_name.begin(),
          narrow_name.end());
      adapter_names_.push_back('\0');
    }

    adapter_names_.push_back('\0');
  }

  Window::SizeEvent handle_window_size(
      Swapchain& swapchain,
      std::vector<RenderContext>& render_context_list) {
    if(window_.handle_window_size() == Window::SizeEvent::Changed) {
      swapchain.resize_swapchain(window_.width(), window_.height());
      render_context_list.clear();
      render_context_list.resize(swapchain.image_count());
      for(std::size_t i = 0; i < render_context_list.size(); ++i) {
        auto&& rc = render_context_list[i];
        rc.image(swapchain.image(i));
        rc.image_view(swapchain.image_descriptor(i));
        rc.scissor(0, window_.width(), 0, window_.height());
        rc.viewport(window_.width(), window_.height());
      }
      return Window::SizeEvent::Changed;
    }
    return Window::SizeEvent::None;
  }

  std::vector<CComPtr<IDXGIAdapter>> adapters_;
  int adapter_index_ = 0;
  std::vector<char> adapter_names_;
  Window& window_;
};

static void glfw_error_callback(int error, const char* description) {
  std::cerr << "Glfw Error " << error << ": " << description;
}

int main(int, char**) {
  struct DxReport {
    ~DxReport() {
#ifdef RNDRX_ENABLE_DX12_DEBUG_LAYER
      CComPtr<IDXGIDebug1> xgi_debug;
      if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&xgi_debug)))) {
        xgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
      }
#endif
    }
  } report_on_exit;

  // Setup GLFW window
  glfwSetErrorCallback(glfw_error_callback);
  if(!glfwInit())
    return 1;

  struct CleanupGlfw {
    ~CleanupGlfw() {
      glfwTerminate();
    }
  } cleanup_glfw;

  Window window;
  Application app(window);
  while(app.run())
    ;

  return 0;
}

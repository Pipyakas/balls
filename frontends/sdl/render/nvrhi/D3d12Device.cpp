#include "render/nvrhi/GraphicsDevice.h"
#include "render/nvrhi/MessageLog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <nvrhi/d3d12.h>

#include <SDL3/SDL_properties.h>

#include <array>
#include <format>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT backBufferCount = 3;
constexpr DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

std::string narrow(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(length - 1, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}

class D3d12Device final : public GraphicsDevice {
public:
    explicit D3d12Device(SDL_Window* window) : window(window) {}
    ~D3d12Device() override;

    bool initialize(std::string& error);
    nvrhi::IDevice* device() const override { return nvrhiDevice; }
    bool beginFrame() override;
    nvrhi::ITexture* backBuffer() const override { return backBuffers[swapChain->GetCurrentBackBufferIndex()]; }
    void present() override;
    bool setVsync(bool enabled) override;
    bool isVsyncEnabled() const override { return vsync; }
    nvrhi::Format backBufferFormat() const override { return nvrhi::Format::RGBA8_UNORM; }
    std::string description() const override { return std::format("D3D12 {}", adapterName); }

private:
    bool createBackBuffers();
    void releaseBackBuffers();

    SDL_Window* window;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> nativeDevice;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    nvrhi::DeviceHandle nvrhiDevice;
    std::array<nvrhi::TextureHandle, backBufferCount> backBuffers;
    std::string adapterName;
    UINT width = 0;
    UINT height = 0;
    bool tearingSupported = false;
    bool vsync = true;
};

D3d12Device::~D3d12Device() {
    if (nvrhiDevice) {
        nvrhiDevice->waitForIdle();
    }
    releaseBackBuffers();
    nvrhiDevice = nullptr;
}

bool D3d12Device::initialize(std::string& error) {
    HWND windowHandle = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!windowHandle) {
        error = "window has no HWND";
        return false;
    }
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        error = "CreateDXGIFactory2 failed";
        return false;
    }
    for (UINT index = 0;
         factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) !=
         DXGI_ERROR_NOT_FOUND;
         ++index) {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&nativeDevice)))) {
            adapterName = narrow(description.Description);
            break;
        }
    }
    if (!nativeDevice) {
        error = "no D3D12 feature level 12.0 adapter";
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(nativeDevice->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)))) {
        error = "CreateCommandQueue failed";
        return false;
    }
    BOOL allowTearing = FALSE;
    tearingSupported = SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                                                              sizeof(allowTearing))) &&
                       allowTearing;

    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    width = static_cast<UINT>(std::max(pixelWidth, 1));
    height = static_cast<UINT>(std::max(pixelHeight, 1));
    DXGI_SWAP_CHAIN_DESC1 swapChainDescription{};
    swapChainDescription.Width = width;
    swapChainDescription.Height = height;
    swapChainDescription.Format = swapChainFormat;
    swapChainDescription.SampleDesc.Count = 1;
    swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDescription.BufferCount = backBufferCount;
    swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDescription.Flags = tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), windowHandle, &swapChainDescription, nullptr, nullptr,
                                               &swapChain1)) ||
        FAILED(swapChain1.As(&swapChain))) {
        error = "CreateSwapChainForHwnd failed";
        return false;
    }
    factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);

    nvrhi::d3d12::DeviceDesc deviceDescription;
    deviceDescription.errorCB = &MessageLog::instance();
    deviceDescription.pDevice = nativeDevice.Get();
    deviceDescription.pGraphicsCommandQueue = queue.Get();
    nvrhiDevice = nvrhi::d3d12::createDevice(deviceDescription);
    if (!nvrhiDevice) {
        error = "nvrhi::d3d12::createDevice failed";
        return false;
    }
    return createBackBuffers();
}

bool D3d12Device::createBackBuffers() {
    for (UINT index = 0; index < backBufferCount; ++index) {
        ComPtr<ID3D12Resource> resource;
        if (FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&resource)))) {
            return false;
        }
        nvrhi::TextureDesc description;
        description.width = width;
        description.height = height;
        description.format = backBufferFormat();
        description.debugName = "back buffer";
        description.initialState = nvrhi::ResourceStates::Present;
        description.keepInitialState = true;
        description.isRenderTarget = true;
        backBuffers[index] = nvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource,
                                                                       nvrhi::Object(resource.Get()), description);
    }
    return true;
}

void D3d12Device::releaseBackBuffers() {
    for (nvrhi::TextureHandle& buffer : backBuffers) {
        buffer = nullptr;
    }
}

bool D3d12Device::beginFrame() {
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return false;
    }
    if (static_cast<UINT>(pixelWidth) != width || static_cast<UINT>(pixelHeight) != height) {
        nvrhiDevice->waitForIdle();
        releaseBackBuffers();
        nvrhiDevice->runGarbageCollection();
        width = static_cast<UINT>(pixelWidth);
        height = static_cast<UINT>(pixelHeight);
        if (FAILED(swapChain->ResizeBuffers(backBufferCount, width, height, swapChainFormat,
                                            tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0)) ||
            !createBackBuffers()) {
            return false;
        }
    }
    return true;
}

void D3d12Device::present() {
    const UINT flags = !vsync && tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0;
    swapChain->Present(vsync ? 1 : 0, flags);
}

bool D3d12Device::setVsync(bool enabled) {
    vsync = enabled;
    return true;
}

}

std::unique_ptr<GraphicsDevice> createD3d12GraphicsDevice(SDL_Window* window, std::string& error) {
    auto device = std::make_unique<D3d12Device>(window);
    if (!device->initialize(error)) {
        return nullptr;
    }
    return device;
}
